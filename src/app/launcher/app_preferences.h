#pragma once
#include "../app_preferences.h"
namespace app_preferences {
using Catalog = AppPreferences;
Catalog load();
bool save(const Catalog&, std::string& error);
}
