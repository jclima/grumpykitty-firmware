#include "app_preferences.h"
#include <Preferences.h>

namespace app_preferences {
Catalog load() {
    Catalog catalog;
    Preferences storage;
    if (!storage.begin("mk_app_ui", true)) return catalog;
    const String raw = storage.getString("catalog", "");
    storage.end();
    if (raw.length()) catalog.load(raw.c_str());
    return catalog;
}

bool save(const Catalog& catalog, std::string& error) {
    const std::string encoded = catalog.encode();
    Catalog validation;
    if (!validation.load(encoded.c_str())) { error = "Invalid app preferences"; return false; }
    if (encoded.size() > 4096) { error = "App preferences are full"; return false; }
    Preferences storage;
    if (!storage.begin("mk_app_ui", false)) { error = "Settings storage unavailable"; return false; }
    const String previous = storage.getString("catalog", "");
    const bool saved = encoded == previous.c_str() || storage.putString("catalog", encoded.c_str()) == encoded.size();
    storage.end();
    if (!saved) error = "Could not save app preferences";
    return saved;
}
}
