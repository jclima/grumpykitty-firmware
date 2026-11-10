#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include "../../src/app/app_09/infrared_file_utils.h"

namespace {
enum class Fault { none, write, read, flush, sync, close, corrupt, rename, publish, restore, cleanup, publishCorrupt };
Fault fault = Fault::none;
bool failSourceOpen = false;
int renameCalls = 0;
bool tripped = false;
auto base() { return infrared_file::defaultTransactionOps(); }
FILE* badOpen(const char* p, const char* mode) {
    if (failSourceOpen && std::strstr(mode, "rb") && std::strstr(p, ".tmp") == nullptr) return nullptr;
    return std::fopen(p, mode);
}
size_t badWrite(const void* p, size_t s, size_t n, FILE* f) {
    if (fault == Fault::write && !tripped) { tripped = true; return n ? n - 1 : 0; }
    if (fault == Fault::corrupt && !tripped && s == 1 && n) {
        std::vector<unsigned char> copy(static_cast<const unsigned char*>(p), static_cast<const unsigned char*>(p) + n);
        copy[0] ^= 0x5a; tripped = true; return std::fwrite(copy.data(), s, n, f);
    }
    return std::fwrite(p, s, n, f);
}
size_t badRead(void* p, size_t s, size_t n, FILE* f) {
    if (fault == Fault::read && !tripped && n > 1) { tripped = true; return 0; }
    return std::fread(p, s, n, f);
}
int badFlush(FILE* f) { return fault == Fault::flush && !tripped ? (tripped = true, -1) : std::fflush(f); }
int badSync(FILE* f) { return fault == Fault::sync && !tripped ? (tripped = true, -1) : ::fsync(::fileno(f)); }
int badClose(FILE* f) { return fault == Fault::close && !tripped ? (tripped = true, std::fclose(f), -1) : std::fclose(f); }
int badRename(const char* a, const char* b) {
    ++renameCalls;
    FILE* pre = std::fopen(b, "rb"); const bool destBefore = pre != nullptr; if (pre) std::fclose(pre);
    const bool fail = (fault == Fault::rename && renameCalls == 1) ||
                      (fault == Fault::publish && renameCalls == 2) ||
                      (fault == Fault::restore && (renameCalls == 2 || renameCalls == 3));
    if (fail) return -1;
    const int result = std::rename(a, b);
    if (result == 0 && fault == Fault::publishCorrupt && std::strstr(b, ".bak") == nullptr && (!destBefore || renameCalls == 2) && !tripped) {
        FILE* f = std::fopen(b, "r+b");
        if (f) { int c = std::fgetc(f); std::rewind(f); std::fputc(c ^ 0x5a, f); std::fclose(f); }
        tripped = true;
    }
    return result;
}
int badRemove(const char* p) { if (fault == Fault::cleanup && std::strstr(p, ".bak")) return -1; return std::remove(p); }
infrared_file::TransactionOps ops() {
    auto o = base();
    o.write = badWrite; o.read = badRead; o.flush = badFlush; o.sync = badSync;
    o.close = badClose; o.rename = badRename;
    o.remove = badRemove;
    o.open = badOpen;
    return o;
}
void clean(const char* p) { auto o = base(); o.remove(p); std::string x = std::string(p) + ".tmp"; o.remove(x.c_str()); x = std::string(p) + ".bak"; o.remove(x.c_str()); x = std::string(p) + ".recovery"; o.remove(x.c_str()); }
std::string readAll(const char* p) { FILE* f = std::fopen(p, "rb"); if (!f) return {}; std::string s; char b[128]; size_t n; while ((n = std::fread(b, 1, sizeof(b), f))) s.append(b, n); std::fclose(f); return s; }
}

int main() {
    char dir[] = "/tmp/ir-transaction-XXXXXX";
    assert(mkdtemp(dir));
    char pathBuf[256];
    assert(std::snprintf(pathBuf, sizeof(pathBuf), "%s/file", dir) > 0);
    const char* path = pathBuf;
    clean(path);
    auto normal = base();
    const char* original = "Filetype: IR signals file\nVersion: 1\n#\n";
    assert(infrared_file::transactionalWrite(path, original, std::strlen(original), false, &normal) == infrared_file::TransactionResult::Saved);
    const std::string before = readAll(path);
    const char* suffix = "\n#\nname: Power\n";
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &normal) == infrared_file::TransactionResult::Saved);
    assert(readAll(path) == before + suffix);
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &normal) == infrared_file::TransactionResult::Saved);
    assert(readAll(path) == before + suffix + suffix);

    const Fault failures[] = {Fault::write, Fault::read, Fault::flush, Fault::sync, Fault::close, Fault::corrupt, Fault::rename};
    for (Fault f : failures) {
        clean(path); assert(infrared_file::transactionalWrite(path, original, std::strlen(original), false, &normal) == infrared_file::TransactionResult::Saved);
        fault = f; tripped = false; renameCalls = 0;
        auto injected = ops();
        const auto result = infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &injected);
        assert(result != infrared_file::TransactionResult::Saved && result != infrared_file::TransactionResult::SavedBackupRetained);
        assert(readAll(path) == original || readAll((std::string(path) + ".bak").c_str()) == original);
    }
    clean(path); assert(infrared_file::transactionalWrite(path, original, std::strlen(original), false, &normal) == infrared_file::TransactionResult::Saved);
    fault = Fault::publish; tripped = false; renameCalls = 0;
    auto publishFailure = ops();
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &publishFailure) != infrared_file::TransactionResult::Saved);
    assert(readAll(path) == original || readAll((std::string(path) + ".bak").c_str()) == original);
    clean(path); assert(infrared_file::transactionalWrite(path, original, std::strlen(original), false, &normal) == infrared_file::TransactionResult::Saved);
    fault = Fault::restore; tripped = false; renameCalls = 0;
    auto restoreFailure = ops();
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &restoreFailure) == infrared_file::TransactionResult::RecoveryRequired);
    assert(readAll(path) == original || readAll((std::string(path) + ".bak").c_str()) == original);
    clean(path); assert(infrared_file::transactionalWrite(path, original, std::strlen(original), false, &normal) == infrared_file::TransactionResult::Saved);
    fault = Fault::cleanup; tripped = false; renameCalls = 0;
    auto cleanupFailure = ops();
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &cleanupFailure) == infrared_file::TransactionResult::SavedBackupRetained);
    assert(readAll(path) == before + suffix || readAll(path) == std::string(original) + suffix);
    clean(path); assert(infrared_file::transactionalWrite(path, original, std::strlen(original), false, &normal) == infrared_file::TransactionResult::Saved);
    failSourceOpen = true; auto sourceFailure = ops();
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &sourceFailure) == infrared_file::TransactionResult::Failed);
    assert(!std::fopen((std::string(path) + ".tmp").c_str(), "rb"));
    failSourceOpen = false;
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &normal) == infrared_file::TransactionResult::Saved);
    clean(path); fault = Fault::publishCorrupt; tripped = false; renameCalls = 0;
    auto saveCorrupt = ops();
    const auto saveResult = infrared_file::transactionalWrite(path, original, std::strlen(original), false, &saveCorrupt);
    assert(saveResult == infrared_file::TransactionResult::RecoveryRequired);
    clean(path); assert(infrared_file::transactionalWrite(path, original, std::strlen(original), false, &normal) == infrared_file::TransactionResult::Saved);
    fault = Fault::publishCorrupt; tripped = false; renameCalls = 0;
    auto appendCorrupt = ops();
    assert(infrared_file::transactionalWrite(path, suffix, std::strlen(suffix), true, &appendCorrupt) == infrared_file::TransactionResult::Failed);
    assert(readAll(path) == original || readAll((std::string(path) + ".bak").c_str()) == original);
    clean(path);
    assert(::rmdir(dir) == 0);
    return 0;
}
