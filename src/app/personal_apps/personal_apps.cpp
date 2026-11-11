#include "personal_apps.h"
#include "personal_apps_validation.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SD_MMC.h>

#include "../app_common/sd_guard.h"
#include "lua_app.h"
#include "../app_14/sk_client.h"

#include <algorithm>
namespace personal_apps {
namespace {
constexpr char kRoot[] = "/apps";
constexpr size_t kMaxManifest = 4096;
constexpr size_t kMaxPackages = 24;
constexpr size_t kMaxScanEntries = 256;

bool readFile(const char* path, std::string& out, size_t max, std::string& err) {
    File f = SD_MMC.open(path, FILE_READ);
    if (!f || f.isDirectory()) { err = "missing file"; return false; }
    size_t n = f.size();
    if (n == 0 || n > max) { f.close(); err = n ? "file too large" : "empty file"; return false; }
    out.resize(n);
    if (f.read(reinterpret_cast<uint8_t*>(&out[0]), n) != static_cast<int>(n)) { f.close(); err = "read failed"; return false; }
    f.close();
    return true;
}

Package inspect(const std::string& id, const std::string& path) {
    Package p; p.id = id; p.path = path;
    std::string text, err;
    if (!validation::validId(id)) { p.error = "invalid package id"; return p; }
    std::string manifest = path + "/manifest.json";
    if (!readFile(manifest.c_str(), text, kMaxManifest, err)) { p.error = "manifest: " + err; return p; }
    validation::ManifestMetadata metadata;
    if (!validation::validateManifestJson(text, id, metadata, p.error)) return p;
    p.name = metadata.name; p.version = metadata.version; p.description = metadata.description;
    p.wifi = metadata.wifi; p.sound = metadata.sound; p.signalk = metadata.signalk;
    File main = SD_MMC.open((path + "/main.lua").c_str(), FILE_READ);
    if (!main || main.isDirectory() || main.size() == 0 || main.size() > 64 * 1024) { p.error = "main.lua size invalid"; if (main) main.close(); return p; }
    std::string source; source.resize(main.size());
    const bool readOk = main.read(reinterpret_cast<uint8_t*>(&source[0]), source.size()) == static_cast<int>(source.size());
    main.close();
    if (!readOk || !validation::sourceScript(source)) { p.error = "main.lua must be Lua source text"; return p; }
    p.compatible = true; return p;
}

bool preflightTree(const std::string& path, unsigned maxDepth, unsigned maxEntries, std::string& error, unsigned depth = 0, unsigned* count = nullptr) {
    unsigned localCount = 0;
    if (!count) count = &localCount;
    if (depth > maxDepth || *count > maxEntries) { error = "package tree too large"; return false; }
    File dir = SD_MMC.open(path.c_str());
    if (!dir || !dir.isDirectory()) { error = "package folder missing"; if (dir) dir.close(); return false; }
    File entry;
    while ((entry = dir.openNextFile())) {
        if (depth >= maxDepth) { entry.close(); dir.close(); error = "package tree too deep"; return false; }
        ++*count;
        const bool directory = entry.isDirectory();
        std::string child = path + "/" + entry.name();
        entry.close();
        if (*count > maxEntries || (directory && !preflightTree(child, maxDepth, maxEntries, error, depth + 1, count))) { dir.close(); if (error.empty()) error = "package tree too large"; return false; }
    }
    dir.close();
    return true;
}

bool removeTree(const std::string& path, std::string& error) {
    File dir = SD_MMC.open(path.c_str());
    if (!dir || !dir.isDirectory()) { error = "package folder missing"; if (dir) dir.close(); return false; }
    File entry;
    while ((entry = dir.openNextFile())) {
        std::string child = path + "/" + entry.name();
        const bool directory = entry.isDirectory();
        entry.close();
        const bool ok = directory ? removeTree(child, error) : SD_MMC.remove(child.c_str());
        if (!ok) { dir.close(); if (error.empty()) error = "remove failed"; return false; }
    }
    dir.close();
    if (!SD_MMC.rmdir(path.c_str())) { error = "remove folder failed"; return false; }
    return true;
}
} // namespace

ScanResult scan() {
    ScanResult result;
    const sd_guard::Status sd = sd_guard::check();
    if (sd != sd_guard::Status::Ok) { result.error = sd_guard::message(sd); return result; }
    File root = SD_MMC.open(kRoot);
    if (!root) { if (!SD_MMC.mkdir(kRoot)) { result.error = "cannot create /apps"; return result; } root = SD_MMC.open(kRoot); }
    if (!root || !root.isDirectory()) { result.error = "/apps is not a directory"; return result; }
    File e;
    size_t visited = 0;
    while (visited < kMaxScanEntries && (e = root.openNextFile())) {
        ++visited;
        if (result.packages.size() < kMaxPackages && e.isDirectory()) {
            std::string id = e.name();
            if (!id.empty() && id[0] != '.') result.packages.push_back(inspect(id, std::string(kRoot) + "/" + id));
        }
        e.close();
    }
    if (visited == kMaxScanEntries) result.warning = "SD app scan truncated at 256 entries";
    root.close();
    std::sort(result.packages.begin(), result.packages.end(), [](const Package& a, const Package& b) { return a.id < b.id; });
    return result;
}

bool remove(const Package& package, bool deleteData, std::string& error) {
    if (!validation::validId(package.id) || package.path != std::string(kRoot) + "/" + package.id) { error = "untrusted package path"; return false; }
    if (sd_guard::check() != sd_guard::Status::Ok) { error = sd_guard::message(sd_guard::check()); return false; }
    if (!preflightTree(package.path, 4, 64, error)) return false;
    std::string data;
    if (deleteData) {
        data = "/appdata/" + package.id;
        if (SD_MMC.exists(data.c_str()) && !preflightTree(data, 8, 512, error)) return false;
    }
    if (deleteData) {
        if (SD_MMC.exists(data.c_str())) {
            if (!removeTree(data, error)) return false;
        }
    }
    if (!removeTree(package.path, error)) return false;
    return true;
}

std::unique_ptr<mooncake::AppAbility> makeApp(DEVICES* d, const Package& p) {
    if (sd_guard::check() != sd_guard::Status::Ok || !p.compatible || !validation::validId(p.id) || p.path != std::string(kRoot) + "/" + p.id) return nullptr;
    Package checked = inspect(p.id, p.path);
    if (!checked.compatible) return nullptr;
    return std::make_unique<LuaApp>(d, checked);
}

bool cleanupPending() { return sk::workerCleanupPending(); }
} // namespace personal_apps
