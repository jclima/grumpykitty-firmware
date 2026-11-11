#pragma once

#include <memory>
#include <string>
#include <vector>

#include <mooncake.h>

#include "../../bsp/devices.h"

namespace personal_apps {

struct Package {
    std::string id, name, version, description, path, error;
    // Capabilities are persisted from the manifest so the runtime can expose
    // only the bridges the package explicitly requested.
    bool wifi = false;
    bool sound = false;
    bool signalk = false;
    bool compatible = false;
};

struct ScanResult {
    std::vector<Package> packages;
    std::string error;
    std::string warning;
};

ScanResult scan();
bool remove(const Package& package, bool deleteData, std::string& error);
std::unique_ptr<mooncake::AppAbility> makeApp(DEVICES*, const Package&);
bool cleanupPending();

} // namespace personal_apps
