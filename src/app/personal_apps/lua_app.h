#pragma once
#include "personal_apps.h"

namespace personal_apps {
class LuaApp final : public mooncake::AppAbility {
public:
    LuaApp(DEVICES*, const Package&);
    ~LuaApp() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace personal_apps
