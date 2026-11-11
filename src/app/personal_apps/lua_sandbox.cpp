#include "lua_sandbox.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace personal_apps {

void* LuaSandbox::allocate(void* ud, void* ptr, size_t oldSize, size_t newSize) {
    auto* self = static_cast<LuaSandbox*>(ud);
    const size_t current = ptr ? oldSize : 0; // oldSize is a Lua type tag when ptr is null.
    if (newSize == 0) {
        if (ptr) {
            std::free(ptr);
            self->used_ -= std::min(self->used_, current);
        }
        return nullptr;
    }
    if (newSize > current && self->used_ + (newSize - current) > kHeapLimit) return nullptr;
    void* next = std::realloc(ptr, newSize);
    if (!next) return nullptr;
    self->used_ = self->used_ - std::min(self->used_, current) + newSize;
    return next;
}

void LuaSandbox::instructionHook(lua_State* L, lua_Debug*) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    auto* self = static_cast<LuaSandbox*>(ud);
    self->instructions_ += 1000;
    if (self->instructions_ > kInstructionLimit) luaL_error(L, "instruction budget exceeded");
}

LuaSandbox::LuaSandbox() {
    state_ = lua_newstate(&allocate, this);
    if (state_) lua_sethook(state_, &instructionHook, LUA_MASKCOUNT, 1000);
}

LuaSandbox::~LuaSandbox() {
    if (state_) lua_close(state_);
    state_ = nullptr;
}

void LuaSandbox::captureError() {
    size_t length = 0;
    const char* message = state_ && lua_type(state_, -1) == LUA_TSTRING
        ? lua_tolstring(state_, -1, &length) : nullptr;
    if (!message) { message = "lua call failed"; length = std::strlen(message); }
    length = std::min(length, sizeof(error_) - 1);
    std::memcpy(error_, message, length);
    error_[length] = '\0';
}

bool LuaSandbox::call(lua_CFunction trampoline, void* context) {
    error_[0] = '\0';
    instructions_ = 0;
    if (!state_ || !trampoline || !lua_checkstack(state_, 3)) {
        std::strcpy(error_, "sandbox unavailable");
        return false;
    }
    lua_pushcfunction(state_, trampoline);
    lua_pushlightuserdata(state_, context);
    if (lua_pcall(state_, 1, 0, 0) != LUA_OK) {
        captureError();
        lua_settop(state_, 0);
        return false;
    }
    lua_settop(state_, 0);
    return true;
}

} // namespace personal_apps
