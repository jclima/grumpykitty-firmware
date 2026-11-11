#include <cassert>
#include <string>

#include "../../src/app/personal_apps/personal_apps_validation.h"

int main() {
    using personal_apps::validation::boundedUtf8Field;
    using personal_apps::validation::sourceScript;
    using personal_apps::validation::validId;

    assert(validId("boat-dash"));
    assert(validId("a.1_b-2"));
    assert(!validId(".hidden"));
    assert(!validId("-hidden"));
    assert(!validId("_hidden"));
    assert(!validId("a..b"));
    assert(!validId("A"));
    assert(!validId(std::string(32, 'a')));

    assert(boundedUtf8Field("x", 1, 39));
    assert(!boundedUtf8Field("", 1, 39));
    assert(!boundedUtf8Field(std::string(40, 'x'), 1, 39));
    assert(sourceScript("return {}"));
    assert(!sourceScript(""));
    assert(!sourceScript("\x1bLua"));
    assert(!sourceScript(std::string(65537, 'x')));

    auto manifest = [](const std::string& extra) {
        return std::string("{\"api_version\":1,\"id\":\"demo\",\"entry\":\"main.lua\",\"name\":\"Demo\",\"version\":\"1\",\"description\":\"A demo\",\"capabilities\":[]") + extra + "}";
    };
    personal_apps::validation::ManifestMetadata parsed;
    std::string error;
    assert(personal_apps::validation::validateManifestJson(manifest(""), "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson("{", "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson(manifest(",\"api_version\":true"), "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson(manifest(",\"api_version\":1.5"), "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson(manifest(",\"entry\":\"other.lua\""), "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson(manifest(",\"capabilities\":[\"wifi\",\"wifi\"]"), "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson(manifest(",\"capabilities\":[\"network\"]"), "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson(manifest(",\"name\":\"" + std::string(40, 'x') + "\""), "demo", parsed, error));
    assert(!personal_apps::validation::validateManifestJson(manifest(""), "other", parsed, error));
    return 0;
}
