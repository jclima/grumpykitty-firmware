/**
 * @file persist.cpp
 * @brief NVS Key-Value storage — Arduino Preferences wrapper (uses ESP-IDF nvs_flash).
 *
 * Schema versioning: persist_init() compares the stored PKEY_NVS_VER against
 * PERSIST_SCHEMA_VERSION and wipes the namespace on mismatch, ensuring clean
 * migration when keys are added or renamed between firmware releases.
 */
#include "persist.h"
#include <Preferences.h>
#include <Arduino.h>
#include <esp_system.h>

static Preferences s_prefs;
static bool        s_open = false;

static constexpr char kNS[] = "mk_cfg";
static constexpr char kWifiMigrationStage[] = "wp_mig";

/* ── WiFi-password obfuscation ───────────────────────────────────
 * XOR with a 32-bit key derived from the chip's unique eFuse MAC.
 * Not cryptographic, but prevents casual Flash-dump reads.
 * Stored as a raw blob (NVS_TYPE_BLOB) rather than a string so that
 * XOR'd bytes containing 0x00 do not truncate the stored value.     */

static uint32_t _chip_key(void)
{
    uint64_t mac = ESP.getEfuseMac();
    return (uint32_t)(mac ^ (mac >> 32));
}

static void _xor_pass(uint8_t * buf, size_t len)
{
    const uint32_t key = _chip_key();
    const uint8_t  kb[4] = {
        (uint8_t)( key        & 0xFF),
        (uint8_t)((key >>  8) & 0xFF),
        (uint8_t)((key >> 16) & 0xFF),
        (uint8_t)((key >> 24) & 0xFF),
    };
    for (size_t i = 0; i < len; i++) buf[i] ^= kb[i & 3];
}

static bool _verify_blob(const char *key, const uint8_t *expected, size_t len)
{
    if (s_prefs.getType(key) != PT_BLOB || s_prefs.getBytesLength(key) != len) {
        return false;
    }
    uint8_t actual[65] = {};
    if (len > sizeof(actual) || s_prefs.getBytes(key, actual, sizeof(actual)) != len) {
        return false;
    }
    return memcmp(actual, expected, len) == 0;
}

/* Remove every entry stored under `key`, whatever its NVS type.  NVS keys are
 * type-tagged, so a legacy STR and a newer BLOB can coexist; each remove()
 * drops one entry.  Bounded at two passes.  Returns true only if nothing is
 * left under the key. */
static bool _remove_all_types(const char *key)
{
    for (int i = 0; i < 2 && s_prefs.isKey(key); i++) {
        if (!s_prefs.remove(key)) return false;
    }
    return !s_prefs.isKey(key);
}

/* Replace whatever is stored as wifi_pass with `encoded` as a single blob,
 * keeping a staged copy in wp_mig so a power cut can resume.  The caller
 * removes the stage afterwards. */
static bool _replace_wifi_pass(const uint8_t *encoded, size_t len, bool staged)
{
    if (!staged && (s_prefs.putBytes(kWifiMigrationStage, encoded, len) != len ||
                    !_verify_blob(kWifiMigrationStage, encoded, len))) {
        return false;
    }
    if (!_remove_all_types(PKEY_WIFI_PASS)) return false;
    return s_prefs.putBytes(PKEY_WIFI_PASS, encoded, len) == len &&
           _verify_blob(PKEY_WIFI_PASS, encoded, len);
}

static bool _migrate_v2_wifi_pass(void)
{
    /* A staged blob lets an interrupted type replacement resume without
     * losing the source value.  The stage is intentionally short for NVS. */
    uint8_t encoded[64] = {};
    size_t len = 0;
    bool staged = s_prefs.isKey(kWifiMigrationStage);
    if (staged) {
        len = s_prefs.getBytes(kWifiMigrationStage, encoded, sizeof(encoded));
        if (!len || !_verify_blob(kWifiMigrationStage, encoded, len)) {
            return false;
        }
    } else if (s_prefs.getType(PKEY_WIFI_PASS) == PT_BLOB) {
        len = s_prefs.getBytes(PKEY_WIFI_PASS, encoded, sizeof(encoded));
        if (!len || !_verify_blob(PKEY_WIFI_PASS, encoded, len)) return false;
        return true;
    } else {
        if (s_prefs.getType(PKEY_WIFI_PASS) != PT_STR) {
            return !s_prefs.isKey(PKEY_WIFI_PASS);
        }
        /* Read through String so an over-long v2 value is truncated, not
         * rejected (a fixed buffer makes getString return 0 forever). */
        String old_pass = s_prefs.getString(PKEY_WIFI_PASS, "");
        len = old_pass.length();
        if (len > sizeof(encoded)) len = sizeof(encoded);
        if (len) {
            memcpy(encoded, old_pass.c_str(), len);
            _xor_pass(encoded, len);
        }
    }

    if (!len) {
        /* Empty v2 values are represented by an absent key in v3. */
        return _remove_all_types(PKEY_WIFI_PASS);
    }

    if (_verify_blob(PKEY_WIFI_PASS, encoded, len)) return true;

    /* Persist the obfuscated value before removing the legacy string. */
    return _replace_wifi_pass(encoded, len, staged);
}

/* Schema-3 scrub: a device migrated by older code can hold a plaintext STR
 * wifi_pass next to the blob.  Re-store the authoritative value as a single
 * blob through the wp_mig stage (resumable after power loss).  A stage that is
 * left over while a plain blob is in place is stale and is just dropped; a
 * stage with no wifi_pass at all is the tail of an interrupted scrub. */
static bool _scrub_v3_wifi_pass(void)
{
    bool has_stage = s_prefs.isKey(kWifiMigrationStage);
    bool has_str   = (s_prefs.getType(PKEY_WIFI_PASS) == PT_STR);
    bool has_pass  = s_prefs.isKey(PKEY_WIFI_PASS);
    if (!has_str && !(has_stage && !has_pass)) {
        return true;   /* nothing to scrub; stale stage handled by caller */
    }

    uint8_t encoded[64] = {};
    size_t len = 0;
    if (has_stage) {
        len = s_prefs.getBytes(kWifiMigrationStage, encoded, sizeof(encoded));
        if (!len || !_verify_blob(kWifiMigrationStage, encoded, len)) return false;
    } else {
        size_t blen = s_prefs.getBytesLength(PKEY_WIFI_PASS);
        if (blen > 0) {
            if (blen > sizeof(encoded)) return false;
            len = s_prefs.getBytes(PKEY_WIFI_PASS, encoded, sizeof(encoded));
            if (len != blen) return false;
        } else {
            String old_pass = s_prefs.getString(PKEY_WIFI_PASS, "");
            len = old_pass.length();
            if (len > sizeof(encoded)) len = sizeof(encoded);
            if (len) {
                memcpy(encoded, old_pass.c_str(), len);
                _xor_pass(encoded, len);
            }
        }
    }
    if (!len) return _remove_all_types(PKEY_WIFI_PASS);
    return _replace_wifi_pass(encoded, len, has_stage);
}

bool persist_init(void)
{
    if (s_open) return true;
    s_open = s_prefs.begin(kNS, false);   /* false = read-write */
    if (!s_open) {
        Serial.println("[persist] ERROR: Preferences open failed");
        return false;
    }

    int stored_ver = s_prefs.getInt(PKEY_NVS_VER, 0);
    if (stored_ver == PERSIST_SCHEMA_VERSION) {
        if (!_scrub_v3_wifi_pass()) {
            Serial.println("[persist] WARNING: wifi_pass scrub failed; will retry next boot");
        } else if (s_prefs.isKey(kWifiMigrationStage) && !s_prefs.remove(kWifiMigrationStage)) {
            Serial.println("[persist] WARNING: migration stage cleanup failed");
        }
        return true;
    }

    if (stored_ver == 2 && PERSIST_SCHEMA_VERSION == 3) {
        /* Targeted v2 -> v3 migration: re-encode wifi_pass in place.
         * All other settings (brightness, volume, LED, BLE ...) are kept. */
        Serial.println("[persist] Schema v2 -> v3: migrating wifi_pass");
        if (!_migrate_v2_wifi_pass()) {
            /* Keep the namespace open so every other setting still works;
             * the schema stays v2 and the migration is retried next boot. */
            Serial.println("[persist] ERROR: wifi_pass migration failed; retrying next boot");
            return false;
        }
    } else {
        /* Major version gap or first boot: full namespace erase. */
        Serial.printf("[persist] Schema v%d -> v%d: erasing namespace\n",
                      stored_ver, PERSIST_SCHEMA_VERSION);
        if (!s_prefs.clear()) {
            Serial.println("[persist] ERROR: namespace erase failed; schema unchanged");
            s_prefs.end();
            s_open = false;
            return false;
        }
    }
    if (s_prefs.putInt(PKEY_NVS_VER, PERSIST_SCHEMA_VERSION) <= 0) {
        Serial.println("[persist] ERROR: schema write failed");
        s_prefs.end();
        s_open = false;
        return false;
    }
    if (s_prefs.isKey(kWifiMigrationStage) && !s_prefs.remove(kWifiMigrationStage)) {
        Serial.println("[persist] WARNING: migration stage cleanup failed");
    }
    return true;
}

/* Auto-init on first call so callers don't need to call persist_init() explicitly */
static inline void _ensure_open(void)
{
    if (!s_open) persist_init();
}

/* ── Integer ──────────────────────────────────────────────────── */

int persist_get_int(const char* key, int default_val)
{
    _ensure_open();
    if (!s_open) return default_val;
    return (int)s_prefs.getInt(key, default_val);
}

bool persist_set_int(const char* key, int val)
{
    _ensure_open();
    if (!s_open) return false;
    return s_prefs.putInt(key, val) > 0;
}

/* ── Unsigned 32-bit (packed RGB color, bitfields) ───────────── */

uint32_t persist_get_u32(const char* key, uint32_t default_val)
{
    _ensure_open();
    if (!s_open) return default_val;
    return s_prefs.getUInt(key, default_val);
}

bool persist_set_u32(const char* key, uint32_t val)
{
    _ensure_open();
    if (!s_open) return false;
    return s_prefs.putUInt(key, val) > 0;
}

/* ── String ───────────────────────────────────────────────────── */

bool persist_get_str(const char* key, char* buf, size_t len, const char* default_val)
{
    _ensure_open();
    if (!s_open || !buf || !len) return false;

    if (strcmp(key, PKEY_WIFI_PASS) == 0) {
        /* Stored as XOR-obfuscated blob (NVS_TYPE_BLOB) since schema v3.
         * Fallback to plain getString handles the rare case where the blob
         * key doesn't exist yet (e.g. migration was skipped). */
        uint8_t raw[65] = {};
        size_t  got = s_prefs.getBytes(key, raw, sizeof(raw) - 1);
        if (got == 0) {
            /* Fallback: read as plain string (shouldn't occur after migration) */
            String v = s_prefs.getString(key, default_val ? default_val : "");
            strncpy(buf, v.c_str(), len - 1);
        } else {
            _xor_pass(raw, got);
            strncpy(buf, (const char*)raw, len - 1);
        }
        buf[len - 1] = '\0';
        return true;
    }

    String v = s_prefs.getString(key, default_val ? default_val : "");
    strncpy(buf, v.c_str(), len - 1);
    buf[len - 1] = '\0';
    return true;
}

bool persist_set_str(const char* key, const char* val)
{
    _ensure_open();
    if (!s_open || !val) return false;

    if (strcmp(key, PKEY_WIFI_PASS) == 0) {
        /* Store as XOR-obfuscated blob so the password is not plaintext on Flash. */
        size_t plen = strlen(val);
        if (plen == 0) {
            /* Remove every entry (any type) plus any migration stage copy, so
             * no secret survives a forget. */
            bool ok = _remove_all_types(key);
            if (s_prefs.isKey(kWifiMigrationStage) && !s_prefs.remove(kWifiMigrationStage)) ok = false;
            return ok;
        }
        if (plen > 64) plen = 64;
        uint8_t buf[65] = {};
        memcpy(buf, val, plen);
        _xor_pass(buf, plen);
        /* A legacy plaintext STR entry would survive next to the blob (NVS
         * keys are type-tagged).  Remove it only when present: blob-over-blob
         * stays an atomic NVS replace. */
        for (int i = 0; i < 2 && s_prefs.getType(key) == PT_STR; i++) {
            if (!s_prefs.remove(key)) return false;
        }
        if (s_prefs.getType(key) == PT_STR) return false;
        bool ok = s_prefs.putBytes(key, buf, plen) == plen;
        memset(buf, 0, sizeof(buf));
        if (ok && s_prefs.isKey(kWifiMigrationStage)) s_prefs.remove(kWifiMigrationStage);
        return ok;
    }

    return s_prefs.putString(key, val) > 0;
}

/* ── Erase ────────────────────────────────────────────────────── */

bool persist_erase_key(const char* key)
{
    _ensure_open();
    if (!s_open) return false;
    return s_prefs.remove(key);
}

bool persist_erase_all(void)
{
    _ensure_open();
    if (!s_open) return false;
    return s_prefs.clear();
}
