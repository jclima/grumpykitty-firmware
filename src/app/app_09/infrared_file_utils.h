#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

namespace infrared_file {
enum class TransactionResult : uint8_t {
    Saved,
    SavedBackupRetained,
    Failed,
    RecoveryRequired,
};

/* The transaction engine is deliberately expressed in stdio operations.  The
 * production implementation uses these defaults; tests can replace any
 * operation to exercise short I/O, flush/sync/close, corruption, and rename
 * failures without touching a real card. */
struct TransactionOps {
    FILE* (*open)(const char*, const char*) = std::fopen;
    FILE* (*openExclusive)(const char*) = nullptr;
    size_t (*read)(void*, size_t, size_t, FILE*) = std::fread;
    size_t (*write)(const void*, size_t, size_t, FILE*) = std::fwrite;
    int (*flush)(FILE*) = std::fflush;
    int (*sync)(FILE*) = nullptr;
    int (*close)(FILE*) = std::fclose;
    int (*rename)(const char*, const char*) = std::rename;
    int (*remove)(const char*) = std::remove;
    int (*probe)(const char*) = nullptr; /* 1 exists, 0 absent, -1 inaccessible */
};

inline FILE* stdioOpenExclusive(const char* path) {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return nullptr;
    FILE* f = ::fdopen(fd, "wb");
    if (!f) ::close(fd);
    return f;
}
inline int stdioSync(FILE* f) { return ::fsync(::fileno(f)); }
inline int stdioProbe(const char* path) {
    struct stat st;
    if (::stat(path, &st) == 0) return 1;
    return errno == ENOENT ? 0 : -1;
}
inline TransactionOps defaultTransactionOps() {
    TransactionOps ops;
    ops.openExclusive = stdioOpenExclusive;
    ops.sync = stdioSync;
    ops.probe = stdioProbe;
    return ops;
}

inline bool txWrite(FILE* f, const void* data, size_t len, const TransactionOps& ops) {
    return len == 0 || ops.write(data, 1, len, f) == len;
}
inline bool txVerifyPayload(const char* path, const void* payload, size_t len, const TransactionOps& ops) {
    FILE* f = ops.open(path, "rb");
    bool ok = f != nullptr;
    const uint8_t* p = static_cast<const uint8_t*>(payload);
    size_t left = len;
    while (ok && left) {
        uint8_t got[512]; const size_t n = left < sizeof(got) ? left : sizeof(got);
        if (ops.read(got, 1, n, f) != n || std::memcmp(got, p, n) != 0) ok = false;
        p += n; left -= n;
    }
    uint8_t extra;
    if (ok && (ops.read(&extra, 1, 1, f) != 0 || std::ferror(f) || !std::feof(f))) ok = false;
    if (f && ops.close(f) != 0) ok = false;
    return ok;
}

/* Atomic save/append. `payload` is the complete new file for save, or only the
 * new entry (including its leading newline) for append. */
inline TransactionResult transactionalWrite(const char* dest, const void* payload,
                                            size_t payloadLen, bool append,
                                            const TransactionOps* injected = nullptr) {
    if (!dest || !payload || (!append && payloadLen == 0)) return TransactionResult::Failed;
    TransactionOps ops = injected ? *injected : defaultTransactionOps();
    char tmp[512], bak[512], vacant[512];
    const int tn = std::snprintf(tmp, sizeof(tmp), "%s.tmp", dest);
    const int bn = std::snprintf(bak, sizeof(bak), "%s.bak", dest);
    const int vn = std::snprintf(vacant, sizeof(vacant), "%s.recovery", dest);
    if (tn < 0 || bn < 0 || vn < 0 || static_cast<size_t>(tn) >= sizeof(tmp) ||
        static_cast<size_t>(bn) >= sizeof(bak) || static_cast<size_t>(vn) >= sizeof(vacant))
        return TransactionResult::Failed;
    if (ops.probe(tmp) != 0 || ops.probe(bak) != 0 || ops.probe(vacant) != 0) return TransactionResult::Failed;
    if (append && ops.probe(dest) != 1) return TransactionResult::Failed;
    if (!append && ops.probe(dest) != 0) return TransactionResult::Failed;

    FILE* in = append ? ops.open(dest, "rb") : nullptr;
    if (append && !in) return TransactionResult::Failed;
    FILE* out = ops.openExclusive(tmp);
    if (!out) { if (in) ops.close(in); return TransactionResult::Failed; }
    bool ok = true;
    if (append) {
        uint8_t buf[512];
        while (ok) {
            const size_t n = ops.read(buf, 1, sizeof(buf), in);
            if (n && !txWrite(out, buf, n, ops)) ok = false;
            if (n < sizeof(buf) && !std::feof(in)) ok = false;
            if (!n) { if (std::ferror(in) || !std::feof(in)) ok = false; break; }
        }
    }
    if (ok) ok = txWrite(out, payload, payloadLen, ops);
    if (std::ferror(out)) ok = false;
    if (ops.flush(out) != 0) ok = false;
    if (std::ferror(out)) ok = false;
    if (ops.sync && ops.sync(out) != 0) ok = false;
    if (in && ops.close(in) != 0) ok = false;
    if (ops.close(out) != 0) ok = false;
    if (!ok) { ops.remove(tmp); return TransactionResult::Failed; }

    /* Verify the closed temp byte-for-byte, including exact EOF and all close errors. */
    FILE* verify = ops.open(tmp, "rb");
    FILE* expected = nullptr;
    if (append) {
        expected = ops.open(dest, "rb");
    }
    if (!verify || (append && !expected)) ok = false;
    if (ok && append) {
        uint8_t buf[512];
        for (;;) {
            const size_t n = ops.read(buf, 1, sizeof(buf), expected);
            if (n == 0) { if (std::ferror(expected) || !std::feof(expected)) ok = false; break; }
            uint8_t got[512];
            if (ops.read(got, 1, n, verify) != n || std::memcmp(buf, got, n) != 0) { ok = false; break; }
        }
        if (ok) {
            const uint8_t* suffix = static_cast<const uint8_t*>(payload);
            size_t left = payloadLen;
            while (left) {
                uint8_t got[512];
                const size_t want = left < sizeof(got) ? left : sizeof(got);
                if (ops.read(got, 1, want, verify) != want || std::memcmp(got, suffix, want) != 0) { ok = false; break; }
                suffix += want; left -= want;
            }
            uint8_t extra;
            if (ok && ops.read(&extra, 1, 1, verify) != 0) ok = false;
            if (ok && (std::ferror(verify) || !std::feof(verify))) ok = false;
        }
    } else if (ok) {
        /* For save, compare against the caller's exact payload. */
        const uint8_t* want = static_cast<const uint8_t*>(payload);
        size_t left = payloadLen;
        while (ok && left) {
            uint8_t got[512];
            const size_t n = left < sizeof(got) ? left : sizeof(got);
            if (ops.read(got, 1, n, verify) != n || std::memcmp(got, want, n) != 0) ok = false;
            want += n; left -= n;
        }
        uint8_t extra;
        if (ok && ops.read(&extra, 1, 1, verify) != 0) ok = false;
        if (ok && (std::ferror(verify) || !std::feof(verify))) ok = false;
    }
    if (verify && ops.close(verify) != 0) ok = false;
    if (expected && ops.close(expected) != 0) ok = false;
    if (!ok) { ops.remove(tmp); return TransactionResult::Failed; }

    if (!append) {
        if (ops.rename(tmp, dest) != 0) { ops.remove(tmp); return TransactionResult::Failed; }
        if (!txVerifyPayload(dest, payload, payloadLen, ops)) return TransactionResult::RecoveryRequired;
        return TransactionResult::Saved;
    }
    if (ops.rename(dest, bak) != 0) { ops.remove(tmp); return TransactionResult::Failed; }
    if (ops.rename(tmp, dest) == 0) {
        /* Re-verify the published destination against the retained backup and
         * suffix before allowing backup cleanup. */
        FILE* published = ops.open(dest, "rb");
        FILE* backup = ops.open(bak, "rb");
        bool publishedOk = published && backup;
        if (publishedOk) {
            uint8_t a[512], b[512];
            for (;;) {
                const size_t n = ops.read(a, 1, sizeof(a), backup);
                if (n == 0) { if (std::ferror(backup) || !std::feof(backup)) publishedOk = false; break; }
                if (ops.read(b, 1, n, published) != n || std::memcmp(a, b, n) != 0) { publishedOk = false; break; }
            }
            const uint8_t* suffix = static_cast<const uint8_t*>(payload);
            size_t left = payloadLen;
            while (publishedOk && left) {
                const size_t want = left < sizeof(a) ? left : sizeof(a);
                if (ops.read(b, 1, want, published) != want || std::memcmp(b, suffix, want) != 0) publishedOk = false;
                suffix += want; left -= want;
            }
            uint8_t extra;
            if (publishedOk && ops.read(&extra, 1, 1, published) != 0) publishedOk = false;
            if (publishedOk && (std::ferror(published) || !std::feof(published))) publishedOk = false;
        }
        if (published && ops.close(published) != 0) publishedOk = false;
        if (backup && ops.close(backup) != 0) publishedOk = false;
        if (publishedOk && ops.remove(bak) == 0) return TransactionResult::Saved;
        if (publishedOk) return TransactionResult::SavedBackupRetained;
        /* Verification or cleanup failure: retain all artifacts and restore
         * without ever unlinking the newly published destination. */
        const int destState = ops.probe(dest);
        if (destState < 0) return TransactionResult::RecoveryRequired;
        if (destState == 1 && ops.rename(dest, vacant) != 0) return TransactionResult::RecoveryRequired;
        if (ops.rename(bak, dest) != 0) return TransactionResult::RecoveryRequired;
        return TransactionResult::Failed;
    }
    /* Never unlink a newly published destination. Move it aside before restore. */
    const int destState = ops.probe(dest);
    if (destState < 0) return TransactionResult::RecoveryRequired;
    if (destState == 1 && ops.rename(dest, vacant) != 0) return TransactionResult::RecoveryRequired;
    if (ops.rename(bak, dest) != 0) return TransactionResult::RecoveryRequired;
    return TransactionResult::Failed;
}
static constexpr size_t kMaxRawDurations = 1024;
static constexpr uint64_t kMaxRawUsec = 500000;
static constexpr size_t kMaxRetainedSignals = 256;
static constexpr size_t kMaxRetainedRawBytes = 256 * 1024;
static constexpr size_t kMaxInputBytes = 4 * 1024 * 1024;

inline bool validRaw(const std::vector<uint16_t>& raw) {
    if (raw.empty() || raw.size() > kMaxRawDurations) return false;
    uint64_t total = 0;
    for (uint16_t d : raw) {
        if (d == 0 || total > kMaxRawUsec - d) return false;
        total += d;
    }
    return total <= kMaxRawUsec;
}
// Device-entered names are single FAT path components, never paths.
inline bool validName(const char* name) {
    if (!name || !*name || !std::strcmp(name, ".") || !std::strcmp(name, "..")) return false;
    const size_t n = std::strlen(name);
    if (name[n - 1] == '.' || name[n - 1] == ' ') return false;
    for (const char* p = name; *p; ++p)
        if (static_cast<unsigned char>(*p) < 32 || std::strchr("/\\:*?\"<>|", *p)) return false;
    return true;
}

inline int listWindow(int count, int rows, int& selected, int& offset) {
    if (count <= 0 || rows <= 0) { selected = offset = 0; return 0; }
    offset = std::max(0, std::min(offset, std::max(0, count - rows)));
    const int visible = std::min(rows, count - offset);
    selected = std::max(0, std::min(selected, visible - 1));
    return visible;
}

inline bool hexBytes(const char* p, uint32_t& value) {
    value = 0;
    unsigned bytes = 0;
    while (p && *p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (bytes == 4 || !((*p >= '0' && *p <= '9') ||
            (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
        char* end = nullptr;
        const unsigned long byte = std::strtoul(p, &end, 16);
        if (end == p || end - p > 2 || byte > 255 ||
            (*end && *end != ' ' && *end != '\t')) return false;
        value |= static_cast<uint32_t>(byte) << (8 * bytes++);
        p = end;
    }
    return bytes != 0;
}
} // namespace infrared_file
