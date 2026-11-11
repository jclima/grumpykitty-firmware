#include "../../src/app/personal_apps/lua_sandbox.h"

#include <cassert>
#include <cstring>

using personal_apps::LuaSandbox;

static int healthy(lua_State* L) { lua_pushliteral(L, "ok"); lua_pop(L, 1); return 0; }
static int infinite(lua_State* L) { luaL_loadstring(L, "while true do end"); if (lua_pcall(L, 0, 0, 0) != LUA_OK) return lua_error(L); return 0; }
static int oom(lua_State* L) {
    lua_newtable(L);
    for (int i = 0; i < 200000; ++i) { lua_pushinteger(L, i); lua_rawseti(L, -2, i + 1); }
    return 0;
}
static int metamethod(lua_State* L) {
    lua_newtable(L); lua_newtable(L); lua_pushliteral(L, "bad"); lua_setfield(L, -2, "__index"); lua_setmetatable(L, -2);
    lua_getfield(L, -1, "missing"); return 0;
}
static int numericError(lua_State* L) { lua_pushinteger(L, 42); return lua_error(L); }
static int longError(lua_State* L) { lua_pushlstring(L, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 192); return lua_error(L); }

int main() {
    LuaSandbox sandbox;
    assert(sandbox.state());
    assert(sandbox.call(healthy, nullptr));
    assert(!sandbox.call(infinite, nullptr));
    assert(sandbox.error().find("instruction") != std::string_view::npos);
    assert(sandbox.call(healthy, nullptr));
    assert(!sandbox.call(oom, nullptr));
    assert(sandbox.call(healthy, nullptr));
    assert(!sandbox.call(metamethod, nullptr));
    assert(sandbox.call(healthy, nullptr));
    assert(!sandbox.call(numericError, nullptr));
    assert(sandbox.error() == "lua call failed");
    assert(!sandbox.call(longError, nullptr));
    assert(sandbox.error().size() == 160);
    assert(sandbox.heapUsed() <= LuaSandbox::kHeapLimit);
    return 0;
}
