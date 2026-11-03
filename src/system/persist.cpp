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
        return s_open;   /* nothing to do */
    }

    if (stored_ver == 2 && PERSIST_SCHEMA_VERSION == 3) {
        /* Targeted v2 → v3 migration: re-encode wifi_pass in place.
         * All other settings (brightness, volume, LED, BLE …) are kept. */
        Serial.println("[persist] Schema v2 → v3: migrating wifi_pass");
        String old_pass = s_prefs.getString(PKEY_WIFI_PASS, "");
        /* NVS looks keys up by type: drop the old plaintext string entry so it
         * cannot survive next to (or shadow) the new blob. */
        s_prefs.remove(PKEY_WIFI_PASS);
        if (old_pass.length() > 0) {
            uint8_t buf[65] = {};
            size_t  plen    = old_pass.length();
            if (plen > 64) plen = 64;
            memcpy(buf, old_pass.c_str(), plen);
            _xor_pass(buf, plen);
            s_prefs.putBytes(PKEY_WIFI_PASS, buf, plen);
        }
    } else {
        /* Major version gap or first boot: full namespace erase. */
        Serial.printf("[persist] Schema v%d → v%d: erasing namespace\n",
                      stored_ver, PERSIST_SCHEMA_VERSION);
        s_prefs.clear();
    }
    s_prefs.putInt(PKEY_NVS_VER, PERSIST_SCHEMA_VERSION);
    return s_open;
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
        /* Remove any previous entry first (string or blob): NVS keys are
         * type-tagged, so an old blob would otherwise survive a put of a
         * different type and be returned by persist_get_str(). */
        s_prefs.remove(key);
        if (plen == 0) return true;   /* cleared: key absent -> default "" */
        if (plen > 64) plen = 64;
        uint8_t buf[65] = {};
        memcpy(buf, val, plen);
        _xor_pass(buf, plen);
        return s_prefs.putBytes(key, buf, plen) == plen;
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
