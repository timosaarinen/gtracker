cmake_minimum_required(VERSION 3.25)
project(gtracker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

option(GT_BUILD_TOOLS "Build the document library and command-line renderer" ON)
option(GT_BUILD_EDITOR "Build the SDL3/ImGui native editor" ON)
option(GT_ENABLE_LUA "Enable embedded Lua commands in the tools" ON)
option(GT_BUILD_TESTS "Build native tests" ON)
option(GT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(Threads REQUIRED)

add_library(gtracker_core STATIC src/core.cpp)
target_include_directories(gtracker_core PUBLIC include)
set_target_properties(gtracker_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(gt_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()
gt_warnings(gtracker_core)

if(GT_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    target_compile_options(gtracker_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  else()
    message(FATAL_ERROR "GT_ENABLE_SANITIZERS requires GCC/Clang on a supported Unix host")
  endif()
endif()

if(GT_BUILD_EDITOR AND NOT GT_BUILD_TOOLS)
  message(FATAL_ERROR "GT_BUILD_EDITOR requires GT_BUILD_TOOLS")
endif()

if(GT_BUILD_TOOLS)
  include(cmake/Dependencies.cmake)
  gt_dependency(gt_json json "https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3"
    "0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406")
  add_library(gtracker_project STATIC src/project.cpp)
  target_include_directories(gtracker_project PUBLIC include PRIVATE "${gt_json_SOURCE_DIR}/single_include")
  target_link_libraries(gtracker_project PUBLIC gtracker_core)
  gt_warnings(gtracker_project)

  if(GT_ENABLE_LUA)
    gt_dependency(gt_lua lua "https://www.lua.org/ftp/lua-5.5.1.tar.gz"
      "1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce")
    file(GLOB lua_sources CONFIGURE_DEPENDS "${gt_lua_SOURCE_DIR}/src/*.c")
    list(FILTER lua_sources EXCLUDE REGEX "/lua(c)?\\.c$")
    add_library(gtracker_lua_runtime STATIC ${lua_sources})
    target_include_directories(gtracker_lua_runtime PUBLIC "${gt_lua_SOURCE_DIR}/src")
    if(UNIX)
      target_compile_definitions(gtracker_lua_runtime PRIVATE LUA_USE_POSIX)
      target_link_libraries(gtracker_lua_runtime PUBLIC m ${CMAKE_DL_LIBS})
    endif()
    add_library(gtracker_scripts STATIC src/lua_commands.cpp)
    target_link_libraries(gtracker_scripts PUBLIC gtracker_project PRIVATE gtracker_lua_runtime)
    target_compile_definitions(gtracker_scripts PUBLIC GT_HAS_LUA=1)
  else()
    add_library(gtracker_scripts STATIC src/lua_disabled.cpp)
    target_link_libraries(gtracker_scripts PUBLIC gtracker_project)
    target_compile_definitions(gtracker_scripts PUBLIC GT_HAS_LUA=0)
  endif()
  gt_warnings(gtracker_scripts)

  add_executable(gtracker_cli src/cli.cpp)
  target_link_libraries(gtracker_cli PRIVATE gtracker_scripts)
  target_include_directories(gtracker_cli PRIVATE "${gt_json_SOURCE_DIR}/single_include")
  gt_warnings(gtracker_cli)

  if(GT_BUILD_EDITOR)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    # The pinned tag archive has no .git; never let SDL describe our parent repo.
    set(SDL_REVISION "SDL-3.2.28" CACHE STRING "Pinned SDL source revision" FORCE)
    gt_dependency(gt_sdl sdl "https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.2.28"
      "5c908f07a93087037d3319bc5d820ba8df14ac98bdbdf24af9084ef460ce01b1" CMAKE)
    gt_dependency(gt_imgui imgui "https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.91.9b"
      "8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb")
    gt_dependency(gt_implot implot "https://codeload.github.com/epezent/implot/tar.gz/refs/tags/v0.16"
      "961df327d8a756304d1b0a67316eebdb1111d13d559f0d3415114ec0eb30abd1")
    add_library(gtracker_imgui STATIC
      "${gt_imgui_SOURCE_DIR}/imgui.cpp"
      "${gt_imgui_SOURCE_DIR}/imgui_draw.cpp"
      "${gt_imgui_SOURCE_DIR}/imgui_tables.cpp"
      "${gt_imgui_SOURCE_DIR}/imgui_widgets.cpp"
      "${gt_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
      "${gt_imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
      "${gt_implot_SOURCE_DIR}/implot.cpp"
      "${gt_implot_SOURCE_DIR}/implot_items.cpp")
    target_include_directories(gtracker_imgui PUBLIC "${gt_imgui_SOURCE_DIR}"
      "${gt_imgui_SOURCE_DIR}/backends" "${gt_implot_SOURCE_DIR}")
    target_link_libraries(gtracker_imgui PUBLIC SDL3::SDL3)
    add_executable(gtracker_editor src/editor.cpp)
    target_link_libraries(gtracker_editor PRIVATE gtracker_scripts gtracker_imgui Threads::Threads)
    gt_warnings(gtracker_editor)
  endif()
endif()

if(GT_BUILD_TESTS)
  enable_testing()
  add_executable(gtracker_core_tests tests/core_tests.cpp)
  target_link_libraries(gtracker_core_tests PRIVATE gtracker_core)
  gt_warnings(gtracker_core_tests)
  add_test(NAME core COMMAND gtracker_core_tests)
  add_executable(gtracker_c_header_smoke tests/c_header_smoke.c)
  target_link_libraries(gtracker_c_header_smoke PRIVATE gtracker_core)
  set_target_properties(gtracker_c_header_smoke PROPERTIES LINKER_LANGUAGE CXX)
  add_test(NAME c_header COMMAND gtracker_c_header_smoke)
  if(GT_BUILD_TOOLS)
    add_executable(gtracker_project_tests tests/project_tests.cpp)
    target_link_libraries(gtracker_project_tests PRIVATE gtracker_scripts)
    gt_warnings(gtracker_project_tests)
    add_test(NAME project COMMAND gtracker_project_tests "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME cli_validate COMMAND gtracker_cli validate "${CMAKE_CURRENT_SOURCE_DIR}/examples/campfire.gtrk.json" --json)
    add_test(NAME cli_render COMMAND gtracker_cli render "${CMAKE_CURRENT_SOURCE_DIR}/examples/campfire.gtrk.json"
      --output "${CMAKE_CURRENT_BINARY_DIR}/smoke.wav" --tail 0.5 --json)
    if(GT_ENABLE_LUA)
      add_test(NAME lua_render COMMAND gtracker_cli render "${CMAKE_CURRENT_SOURCE_DIR}/examples/campfire.gtrk.json"
        --script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/soften.lua"
        --output "${CMAKE_CURRENT_BINARY_DIR}/lua-smoke.wav" --tail 0.5 --json)
    endif()
  endif()
endif()
