#pragma once

#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

struct AppPreferences {
    std::vector<std::string> hidden;
    std::vector<std::string> order;

    bool visible(const std::string& id) const {
        return id == "firmware-update" || std::find(hidden.begin(), hidden.end(), id) == hidden.end();
    }

    void toggle(const std::string& id) {
        if (id == "firmware-update") return;
        auto found = std::find(hidden.begin(), hidden.end(), id);
        if (found == hidden.end()) hidden.push_back(id);
        else hidden.erase(found);
    }

    std::vector<std::string> sorted(const std::vector<std::string>& available) const {
        std::vector<std::string> result;
        for (const auto& id : order) {
            if (std::find(available.begin(), available.end(), id) != available.end() &&
                std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
        }
        for (const auto& id : available) {
            if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
        }
        return result;
    }

    bool load(const char* text) {
        JsonDocument document;
        if (strlen(text) > 4096 || deserializeJson(document, text) || !document.is<JsonObject>()) return false;
        AppPreferences candidate;
        if (!readIds(document["hidden"], candidate.hidden) || !readIds(document["order"], candidate.order)) return false;
        *this = std::move(candidate);
        return true;
    }

    std::string encode() const {
        JsonDocument document;
        auto hiddenArray = document["hidden"].to<JsonArray>();
        for (const auto& id : hidden) hiddenArray.add(id);
        auto orderArray = document["order"].to<JsonArray>();
        for (const auto& id : order) orderArray.add(id);
        std::string result;
        serializeJson(document, result);
        return result;
    }

private:
    static bool readIds(JsonVariantConst value, std::vector<std::string>& result) {
        if (!value.is<JsonArrayConst>() || value.size() > 48) return false;
        for (JsonVariantConst entry : value.as<JsonArrayConst>()) {
            if (!entry.is<const char*>()) return false;
            const std::string id = entry.as<const char*>();
            if (id.empty() || id.size() > 31) return false;
            if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
        }
        return true;
    }
};
