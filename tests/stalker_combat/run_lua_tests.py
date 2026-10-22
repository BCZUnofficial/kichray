#!/usr/bin/env python3
"""Run the real zombied script callback in Lua with minimal engine objects."""
from pathlib import Path
import ctypes
import ctypes.util

ROOT = Path(__file__).resolve().parents[2]


def main():
    library = ctypes.util.find_library("lua5.4") or ctypes.util.find_library("lua5.3")
    if not library:
        raise SystemExit("Lua 5.3/5.4 shared library is required for this callback regression check")
    lua = ctypes.CDLL(library)
    lua.luaL_newstate.restype = ctypes.c_void_p
    lua.luaL_openlibs.argtypes = [ctypes.c_void_p]
    lua.luaL_loadbufferx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                                   ctypes.c_char_p, ctypes.c_char_p]
    lua.luaL_loadbufferx.restype = ctypes.c_int
    lua.lua_pcallk.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.c_ssize_t, ctypes.c_void_p]
    lua.lua_pcallk.restype = ctypes.c_int
    lua.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]
    lua.lua_tolstring.restype = ctypes.c_char_p
    lua.lua_close.argtypes = [ctypes.c_void_p]
    state = lua.luaL_newstate()
    if not state:
        raise RuntimeError("Could not create Lua state")

    def run(source, name):
        error = lua.luaL_loadbufferx(state, source, len(source), name.encode(), None)
        if not error:
            error = lua.lua_pcallk(state, 0, 0, 0, 0, None)
        if error:
            message = lua.lua_tolstring(state, -1, None)
            raise AssertionError(message.decode("utf-8", errors="replace") if message else "Lua failure")

    try:
        lua.luaL_openlibs(state)
        run(b'''
property_evaluator = {}
action_base = {}
function class(name)
    local result = {}
    _G[name] = result
    return function(base) setmetatable(result, {__index = base}) end
end
danger_object = {attacked = 1}
''', "engine_stubs")
        # Preserve the original bytes/encoding; the complete source is parsed and loaded.
        run((ROOT / "resources/scripts/xr_combat_zombied.script").read_bytes(), "xr_combat_zombied.script")
        run(b'''
local callback = action_zombie_go_to_danger.hit_callback
local last_position = {x=4, y=2, z=8}
local source = {
    position = function() return last_position end,
    level_vertex_id = function() return 17 end
}
local function check(danger, amount, who, current_action, expected)
    local npc = {
        st = {cur_act = current_action},
        object = {best_danger = function() return danger end},
        was_hit = false
    }
    callback(npc, nil, amount, nil, who, nil)
    assert(npc.was_hit == expected)
    if expected then
        assert(npc.enemy_last_seen_pos == last_position)
        assert(npc.enemy_last_seen_vid == 17)
    else
        assert(npc.enemy_last_seen_pos == nil)
    end
end
local function danger(object, kind)
    return {object = function() return object end, type = function() return kind end}
end
check(danger(nil, 1), 4, {}, 2, false) -- former nil dereference with positive damage
check(danger(nil, 2), 4, {}, 2, false)
check(nil, 4, {}, 2, false)
check(danger(source, 1), 0, {}, 2, true)
check(danger(source, 2), 4, {}, 2, true)
check(danger(source, 2), 0, {}, 2, false)
check(danger(source, 1), 4, nil, 2, false)
check(danger(source, 1), 4, {}, 1, false)
''', "zombied_callback_cases")
    finally:
        lua.lua_close(state)
    print("PASS: complete zombied script syntax and 8 real hit-callback cases")


if __name__ == "__main__":
    main()
