#include "gtracker/project.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace gtracker {
namespace {

Pattern& document(lua_State* state) {
    return *static_cast<Pattern*>(lua_touserdata(state, lua_upvalueindex(1)));
}

// Lua errors use longjmp. Finish every C++ exception handler and destroy its
// locals before calling luaL_error; never throw a C++ exception through Lua.
template <typename Operation>
int edit(lua_State* state, Operation operation) {
    char error[512] = {};
    try {
        operation(document(state));
    } catch (const std::exception& exception) {
        std::snprintf(error, sizeof(error), "%s", exception.what());
    } catch (...) {
        std::snprintf(error, sizeof(error), "%s", "unknown native command error");
    }
    if (error[0] != '\0') {
        return luaL_error(state, "%s", error);
    }
    return 0;
}

int transpose(lua_State* state) {
    const double semitones = luaL_checknumber(state, 1);
    return edit(state, [semitones](Pattern& pattern) {
        if (!std::isfinite(semitones)) {
            throw std::invalid_argument("transpose requires finite semitones");
        }
        for (auto& event : pattern.events) {
            if (event.type == "pluck") {
                event.fret = static_cast<float>(event.fret + semitones);
            }
        }
    });
}

int set_tempo(lua_State* state) {
    const double bpm = luaL_checknumber(state, 1);
    return edit(state, [bpm](Pattern& pattern) { pattern.bpm = bpm; });
}

int scale_strength(lua_State* state) {
    const double factor = luaL_checknumber(state, 1);
    return edit(state, [factor](Pattern& pattern) {
        if (!std::isfinite(factor) || factor < 0.0) {
            throw std::invalid_argument("scale_strength requires a finite nonnegative factor");
        }
        for (auto& event : pattern.events) {
            if (event.type == "pluck" || event.type == "body_hit") {
                event.strength = static_cast<float>(event.strength * factor);
            }
        }
    });
}

int add_note(lua_State* state) {
    const double beat = luaL_checknumber(state, 1);
    const lua_Integer string_number = luaL_checkinteger(state, 2);
    const double fret = luaL_checknumber(state, 3);
    const double strength = luaL_optnumber(state, 4, 0.7);
    return edit(state, [beat, string_number, fret, strength](Pattern& pattern) {
        if (string_number < 1 ||
            string_number > static_cast<lua_Integer>(pattern.tuning_midi.size())) {
            throw std::invalid_argument("add_note string number is outside the instrument");
        }
        Event event;
        std::size_t suffix = pattern.events.size() + 1;
        for (;;) {
            event.id = "lua-" + std::to_string(suffix++);
            bool duplicate = false;
            for (const auto& existing : pattern.events) {
                if (existing.id == event.id) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                break;
            }
        }
        event.type = "pluck";
        event.beat = beat;
        event.string_index = static_cast<int>(string_number - 1);
        event.fret = static_cast<float>(fret);
        event.strength = static_cast<float>(strength);
        pattern.events.push_back(std::move(event));
    });
}

void register_command(lua_State* state, Pattern& pattern, const char* name,
                      lua_CFunction command) {
    lua_pushlightuserdata(state, &pattern);
    lua_pushcclosure(state, command, 1);
    lua_setfield(state, -2, name);
}

} // namespace

void apply_lua(Pattern& pattern, const std::filesystem::path& script_path) {
    if (pattern.revision >= 9007199254740991ULL) {
        throw std::overflow_error("pattern revision exhausted its JSON-safe range");
    }
    Pattern working = pattern;
    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    if (!state) {
        throw std::runtime_error("could not allocate Lua state");
    }

    // These are trusted local scripts, with the ordinary standard libraries.
    // This is intentionally not a sandbox for code obtained from other people.
    luaL_openlibs(state.get());
    lua_newtable(state.get());
    register_command(state.get(), working, "transpose", transpose);
    register_command(state.get(), working, "set_tempo", set_tempo);
    register_command(state.get(), working, "scale_strength", scale_strength);
    register_command(state.get(), working, "add_note", add_note);
    lua_setglobal(state.get(), "gtracker");

    const std::string filename = script_path.string();
    int result = luaL_loadfile(state.get(), filename.c_str());
    if (result == LUA_OK) {
        result = lua_pcall(state.get(), 0, 0, 0);
    }
    if (result != LUA_OK) {
        const char* error = lua_tostring(state.get(), -1);
        throw std::runtime_error(std::string("Lua: ") + (error ? error : "unknown script error"));
    }
    working.revision = pattern.revision + 1;
    validate_pattern(working);
    pattern = std::move(working);
}

} // namespace gtracker
