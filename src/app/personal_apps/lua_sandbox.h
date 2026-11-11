#pragma once

#include <cstddef>
#include <string_view>

#include <lua.hpp>

namespace personal_apps {

class LuaSandbox final {
public:
    static constexpr size_t kHeapLimit = 256 * 1024;
    static constexpr int kInstructionLimit = 100000;

    LuaSandbox();
    ~LuaSandbox();
    LuaSandbox(const LuaSandbox&) = delete;
    LuaSandbox& operator=(const LuaSandbox&) = delete;

    lua_State* state() const { return state_; }
    bool call(lua_CFunction trampoline, void* context);
    std::string_view error() const { return error_; }
    size_t heapUsed() const { return used_; }

private:
    static void* allocate(void*, void*, size_t, size_t);
    static void instructionHook(lua_State*, lua_Debug*);
    void captureError();

    lua_State* state_ = nullptr;
    size_t used_ = 0;
    int instructions_ = 0;
    char error_[161] = {};
};

} // namespace personal_apps
