#include "gtracker/project.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace gtracker {
namespace {

Pattern& document(lua_State* state) {
    return *static_cast<Pattern*>(lua_touserdata(state, lua_upvalueindex(1)));
}

std::string next_id(const Pattern& pattern) {
    for (std::size_t suffix = pattern.events.size() + pattern.gestures.size() + 1;; ++suffix) {
        const std::string id = "lua-" + std::to_string(suffix);
        if (std::none_of(pattern.events.begin(), pattern.events.end(), [&](const Event& event) { return event.id == id; }) &&
            std::none_of(pattern.gestures.begin(), pattern.gestures.end(), [&](const Gesture& gesture) { return gesture.id == id; })) return id;
    }
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
        for (auto& gesture : pattern.gestures) for (auto& point : gesture.points)
            point.fret = static_cast<float>(point.fret + semitones);
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
        event.id = next_id(pattern);
        event.type = "pluck";
        event.beat = beat;
        event.string_index = static_cast<int>(string_number - 1);
        event.fret = static_cast<float>(fret);
        event.strength = static_cast<float>(strength);
        pattern.events.push_back(std::move(event));
    });
}

int set_model(lua_State* state) {
    const char* model = luaL_checkstring(state, 1);
    return edit(state, [model](Pattern& pattern) {
        pattern.schema_version = 2;
        pattern.model = model; // Validation rejects unsupported models/data loss.
    });
}

int add_slide(lua_State* state) {
    const double beat = luaL_checknumber(state, 1);
    const lua_Integer string_number = luaL_checkinteger(state, 2);
    luaL_checktype(state, 3, LUA_TTABLE);
    const auto count = lua_rawlen(state, 3);
    if (count < 2 || count > 256) return luaL_error(state, "add_slide needs 2..256 points");
    // All locals across Lua's checking/longjmp boundary are trivially
    // destructible. Construct owning C++ vectors only inside edit(), afterwards.
    std::array<FretPoint, 256> points{};
    for (std::size_t i = 0; i < count; ++i) {
        lua_rawgeti(state, 3, static_cast<lua_Integer>(i + 1));
        luaL_checktype(state, -1, LUA_TTABLE);
        lua_getfield(state, -1, "beat");
        points[i].beat = luaL_checknumber(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, -1, "fret");
        points[i].fret = static_cast<float>(luaL_checknumber(state, -1));
        lua_pop(state, 2);
    }
    return edit(state, [beat, string_number, points, count](Pattern& pattern) {
        if (pattern.schema_version != 2 || pattern.model != "contact_0_2")
            throw std::invalid_argument("add_slide requires explicit gtracker.set_model('contact_0_2') first");
        if (string_number < 1 || string_number > static_cast<lua_Integer>(pattern.tuning_midi.size()))
            throw std::invalid_argument("add_slide string is outside the instrument");
        Gesture gesture;
        gesture.id = next_id(pattern);
        gesture.beat = beat;
        gesture.string_index = static_cast<int>(string_number - 1);
        gesture.points.assign(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(count));
        pattern.gestures.push_back(std::move(gesture));
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
    register_command(state.get(), working, "set_model", set_model);
    register_command(state.get(), working, "add_slide", add_slide);
    lua_setglobal(state.get(), "gtracker");

    // C stdio's narrow fopen cannot open arbitrary UTF-8 paths on Windows.
    // std::ifstream(path) uses the native filesystem representation instead.
    std::ifstream source(script_path, std::ios::binary);
    if (!source) throw std::runtime_error("Cannot open Lua script: " + script_path.u8string());
    std::string code((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    if (source.bad()) throw std::runtime_error("Cannot read Lua script: " + script_path.u8string());
    std::size_t begin = code.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    // Keep Lua's text-file conveniences and original line numbers. Bytecode is
    // still accepted for trusted local scripts, as with luaL_loadfile.
    if (begin < code.size() && code[begin] == '#') {
        const auto end = code.find('\n', begin);
        begin = end == std::string::npos ? code.size() : end;
    }
    const std::string filename = "@" + script_path.u8string();
    int result = luaL_loadbuffer(state.get(), code.data() + begin, code.size() - begin, filename.c_str());
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
