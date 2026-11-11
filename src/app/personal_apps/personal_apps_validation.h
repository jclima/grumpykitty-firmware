#pragma once

#include <cstddef>
#include <string>
#include <cstring>

#include <ArduinoJson.h>

namespace personal_apps {
namespace validation {

struct ManifestMetadata {
    std::string name;
    std::string version;
    std::string description;
    bool wifi = false;
    bool sound = false;
    bool signalk = false;
};

inline bool validId(const std::string& id) {
    if (id.empty() || id.size() > 31 || id.find("..") != std::string::npos) return false;
    const char first = id.front();
    if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) return false;
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}

inline bool boundedUtf8Field(const std::string& value, size_t minBytes, size_t maxBytes) {
    return value.size() >= minBytes && value.size() <= maxBytes;
}

inline bool knownCapability(const std::string& capability) {
    return capability == "wifi" || capability == "sound" || capability == "signalk";
}

inline bool sourceScript(const std::string& source) {
    return !source.empty() && source.size() <= 64 * 1024 &&
           (source.size() < 4 || source.compare(0, 4, "\x1bLua", 4) != 0);
}

inline bool validateManifestJson(const std::string& text, const std::string& folderId,
                                ManifestMetadata& metadata, std::string& error) {
    JsonDocument doc;
    if (deserializeJson(doc, text) || !doc.is<JsonObject>()) { error = "manifest: invalid JSON"; return false; }
    JsonObject o = doc.as<JsonObject>();
    if (!o["id"].is<const char*>() || folderId != std::string(o["id"].as<const char*>())) { error = "manifest: id does not match folder"; return false; }
    if (!o["entry"].is<const char*>() || strcmp(o["entry"].as<const char*>(), "main.lua") != 0) { error = "manifest: entry must be main.lua"; return false; }
    if (!o["api_version"].is<int>() || o["api_version"].as<int>() != 1) { error = "manifest: api_version must be integer 1"; return false; }
    if (!o["name"].is<const char*>() || !o["version"].is<const char*>() || !o["description"].is<const char*>()) { error = "manifest: name/version/description required"; return false; }
    metadata.name = o["name"].as<const char*>(); metadata.version = o["version"].as<const char*>(); metadata.description = o["description"].as<const char*>();
    if (!boundedUtf8Field(metadata.name, 1, 39) || !boundedUtf8Field(metadata.version, 1, 15) || !boundedUtf8Field(metadata.description, 1, 95)) { error = "manifest: name/version/description length invalid"; return false; }
    JsonArray caps = o["capabilities"].as<JsonArray>();
    if (caps.isNull()) { error = "manifest: capabilities required"; return false; }
    size_t capCount = 0;
    for (JsonVariant c : caps) {
        if (++capCount > 3 || !c.is<const char*>() || !knownCapability(c.as<const char*>())) { error = "manifest: bad capability"; return false; }
        const std::string capability = c.as<const char*>();
        bool* enabled = capability == "wifi" ? &metadata.wifi : (capability == "sound" ? &metadata.sound : &metadata.signalk);
        if (*enabled) { error = "manifest: duplicate capability"; return false; }
        *enabled = true;
    }
    return true;
}

} // namespace validation
} // namespace personal_apps
