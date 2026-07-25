cmake_minimum_required(VERSION 3.20)
project(unnamed_strategy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Find system packages ───────────────────────────────────────────────────────
find_package(SDL2   REQUIRED)
find_package(OpenGL REQUIRED)
if(WIN32)
    find_package(GLEW REQUIRED)
endif()
find_package(SQLite3 REQUIRED)
# Target name changed from SQLite::SQLite3 to SQLite3::SQLite3 in newer CMake
if(TARGET SQLite3::SQLite3)
    set(SQLITE_TARGET SQLite3::SQLite3)
else()
    set(SQLITE_TARGET SQLite::SQLite3)
endif()
find_package(Lua 5.4 REQUIRED)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json system package not found -- fetching v3.11.3 from GitHub")
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ── ImGui (system package with FetchContent fallback) ─────────────────────────
find_path(IMGUI_INCLUDE_DIR
    NAMES imgui.h
    PATH_SUFFIXES imgui
    DOC "Dear ImGui include directory"
)
find_path(IMGUI_BACKENDS_DIR
    NAMES imgui_impl_sdl2.h
    PATH_SUFFIXES imgui/backends imgui
    DOC "Dear ImGui backends include directory"
)
find_library(IMGUI_LIBRARY
    NAMES imgui
    DOC "Dear ImGui library"
)
# stb is a separate system lib on Linux; bundled inside libimgui.a on MSYS2
find_library(STB_LIBRARY NAMES stb)

if(IMGUI_INCLUDE_DIR AND IMGUI_LIBRARY AND EXISTS "${IMGUI_LIBRARY}")
    if(NOT IMGUI_BACKENDS_DIR)
        set(IMGUI_BACKENDS_DIR "${IMGUI_INCLUDE_DIR}/backends")
    endif()

    add_library(imgui_lib INTERFACE)
    target_include_directories(imgui_lib INTERFACE
        ${IMGUI_INCLUDE_DIR}
        ${IMGUI_BACKENDS_DIR}
    )
    target_link_libraries(imgui_lib INTERFACE
        ${IMGUI_LIBRARY}
        $<$<BOOL:${STB_LIBRARY}>:${STB_LIBRARY}>
        ${SDL2_LIBRARIES}
        ${OPENGL_LIBRARIES}
    )
else()
    # No system ImGui — build v1.90.8 from source
    message(STATUS "ImGui system package not found — fetching v1.90.8 from GitHub")
    include(FetchContent)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.90.8
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${SDL2_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIR}
    )
    target_link_libraries(imgui_lib PUBLIC
        ${SDL2_LIBRARIES}
        ${OPENGL_LIBRARIES}
    )
endif()

# ── Game sources ──────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
)
# Exclude per-binary mains — each has its own executable target
list(FILTER SOURCES EXCLUDE REGEX ".*/sim/sim_main\\.cpp$")

add_executable(${PROJECT_NAME} ${SOURCES})

# ── Warnings (game code only — never third_party/imgui) ──────────────────────
# The tree is warning-CLEAN as of 2026-07-25; keeping these on is what stops it
# drifting back. -Wswitch is the one that earns its keep here: it catches a new
# SpellSchool/SpellTarget/SpellEffect enum value that no switch handles, which
# otherwise reads as a spell that silently does nothing while still spending
# the caster's mana. -Wunused-parameter is off: virtual/callback signatures
# legitimately ignore arguments. NOT -Werror — a warning shouldn't block the
# dev's build mid-experiment.
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter)
endif()

# ── Build-time version stamp (git hash/branch/date) logged at startup ─────────
# Regenerated every build so incremental builds via run.sh still get a fresh
# hash; the script only rewrites version_gen.h when the value changes, so
# main.cpp is recompiled only when the commit actually moves.
add_custom_target(gen_version ALL
    BYPRODUCTS ${CMAKE_BINARY_DIR}/version_gen.h
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_SOURCE_DIR}
            -DOUT=${CMAKE_BINARY_DIR}/version_gen.h
            -P ${CMAKE_SOURCE_DIR}/cmake/gen_version.cmake
    COMMENT "Stamping build version"
    VERBATIM)
add_dependencies(${PROJECT_NAME} gen_version)
target_compile_definitions(${PROJECT_NAME} PRIVATE GAME_VERSION="${PROJECT_VERSION}")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party   # stb_image, glm go here
    ${CMAKE_BINARY_DIR}               # generated version_gen.h
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${SQLite3_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${SQLITE_TARGET}
    nlohmann_json::nlohmann_json
    imgui_lib
    ${LUA_LIBRARIES}
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 GLEW::GLEW)
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()

if(WIN32)
    # Ship the RELEASE exe as a GUI-subsystem app so players don't get a stray
    # console window next to the game. Debug/dev builds stay console-subsystem so
    # run.sh's stdout -> `tee session.log` logging keeps working. SDL2's main
    # shim provides the WinMain entry point, so plain main() still works.
    set_target_properties(${PROJECT_NAME} PROPERTIES
                          WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

# ── Steamworks (optional) ─────────────────────────────────────────────────────
# OFF by default so the game builds with zero Steam dependency. Turn on with:
#   cmake -B build -DUSE_STEAMWORKS=ON -DSTEAMWORKS_SDK_ROOT=/path/to/sdk -DSTEAM_APP_ID=1234560
# src/platform/SteamIntegration.cpp is always compiled; without the flag it is
# pure no-ops, so the call sites in main.cpp / the game loop cost nothing.
option(USE_STEAMWORKS "Build with the Steamworks SDK (achievements/cloud/overlay)" OFF)
if(USE_STEAMWORKS)
    if(NOT STEAMWORKS_SDK_ROOT)
        message(FATAL_ERROR "USE_STEAMWORKS=ON requires -DSTEAMWORKS_SDK_ROOT=<path to the extracted Steamworks SDK 'sdk' folder>")
    endif()
    if(NOT DEFINED STEAM_APP_ID)
        set(STEAM_APP_ID 0)
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE ${STEAMWORKS_SDK_ROOT}/public)
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${STEAMWORKS_SDK_ROOT}/redistributable_bin/win64/steam_api64.lib)
    elseif(APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${STEAMWORKS_SDK_ROOT}/redistributable_bin/osx/libsteam_api.dylib)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${STEAMWORKS_SDK_ROOT}/redistributable_bin/linux64/libsteam_api.so)
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_STEAMWORKS STEAM_APP_ID=${STEAM_APP_ID})

    # Bundle the Steam redistributable next to the exe (so dev builds run) and
    # into the install/dist (so the shipped folder runs). Without this a
    # Steamworks build fails to start — missing steam_api64.dll.
    if(WIN32)
        set(_steam_redist "${STEAMWORKS_SDK_ROOT}/redistributable_bin/win64/steam_api64.dll")
    elseif(APPLE)
        set(_steam_redist "${STEAMWORKS_SDK_ROOT}/redistributable_bin/osx/libsteam_api.dylib")
    else()
        set(_steam_redist "${STEAMWORKS_SDK_ROOT}/redistributable_bin/linux64/libsteam_api.so")
    endif()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_steam_redist}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    install(FILES "${_steam_redist}" DESTINATION .)

    message(STATUS "Steamworks: ON (app id ${STEAM_APP_ID}) — redistributable bundled; add steam_appid.txt next to the exe for dev testing")
endif()

# ── Asset sync: mirror source assets/ to the runtime dir on EVERY build ───────
# The game loads from <exe dir>/assets. Two footguns this avoids:
#  * copy_directory only ADDS/updates, never removes -> deleted source assets
#    lingered ("deleted this terrain but still see it"). We wipe first.
#  * a POST_BUILD step only runs when the exe relinks, so newly ADDED art (e.g.
#    freshly generated portraits) never appeared until an unrelated code change.
# A dedicated ALL target that depends on the exe runs the wipe+copy every build,
# so `cmake --build build` always leaves the runtime assets identical to source.
add_custom_target(sync_assets ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMENT "Mirroring assets/ to runtime dir"
)
add_dependencies(sync_assets ${PROJECT_NAME})
# Lua scripts must be COPIED, not just have an empty dir created for them.
# scripts/autoload.lua + campaign.lua exist in the repo and install() ships
# them, but the dev build only ever made an empty build/bin/scripts — so every
# run logged "cannot open scripts/autoload.lua" and the whole Lua trigger /
# campaign-scripting layer silently did nothing in any developer build.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/saves
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/maps
)

# (Both standalone simulators removed. `fullgame_sim` (2026-07-17) reimplemented
#  the AI and measured the wrong thing. `sim_test` — the combat/balance
#  simulator — was removed too: its abstract equal-week matchups did not
#  reflect real play and its "balance" numbers were misleading. The reusable
#  pieces (Simulator/ArmyBuilder) stay in src/sim/ because the in-game map
#  editor's SimulatorWindow and Conquest mode depend on them; only the
#  standalone entry point (src/sim/sim_main.cpp) and its target are gone.)

# ══════════════════════════════════════════════════════════════════════════════
#  PACKAGING — self-contained install + one-click installer
# ══════════════════════════════════════════════════════════════════════════════
# Produces a folder with the exe, all runtime DLLs, and assets that runs on a
# clean machine (no msys2 required). `cmake --install build --prefix dist`
# assembles it; CPack (or the Inno Setup script in packaging/) wraps it.

# 1. The executable
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION .)

# 2. Assets, maps, scripts (campaigns optional)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets   DESTINATION .)
if(EXISTS ${CMAKE_SOURCE_DIR}/maps)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/maps      DESTINATION .)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/scripts)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts   DESTINATION .)
endif()

# 3. Bundle the runtime DLLs the exe depends on (SDL2, sqlite3, lua, glew,
#    plus the gcc/stdc++ runtime from ucrt64). GET_RUNTIME_DEPENDENCIES walks
#    the actual import table so we don't have to hand-list every DLL.
if(WIN32)
    install(CODE "
        include(BundleUtilities)
        set(exe \"\${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.exe\")
        file(GET_RUNTIME_DEPENDENCIES
             EXECUTABLES \"\${exe}\"
             RESOLVED_DEPENDENCIES_VAR deps
             UNRESOLVED_DEPENDENCIES_VAR unresolved
             DIRECTORIES \"C:/msys64/ucrt64/bin\"
             PRE_EXCLUDE_REGEXES \"api-ms-.*\" \"ext-ms-.*\"
             POST_EXCLUDE_REGEXES \".*system32.*\" \".*SysWOW64.*\")
        foreach(dep \${deps})
            file(INSTALL \"\${dep}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
        endforeach()
        if(unresolved)
            message(STATUS \"Bundled DLLs; system libs skipped: \${unresolved}\")
        endif()
    ")
endif()

# 4. CPack — `cpack -G ZIP` (portable) or `-G NSIS`/`-G INNOSETUP` for installers.
set(CPACK_PACKAGE_NAME "UnnamedStrategy")
set(CPACK_PACKAGE_VENDOR "krabysoft")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "HoMM3-style hex strategy game")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "UnnamedStrategy")
if(WIN32)
    # NSIS produces a real one-click Setup.exe. Install it once from msys2:
    #   pacman -S mingw-w64-ucrt-x86_64-nsis
    # Falls back to ZIP only if makensis isn't found at package time.
    find_program(MAKENSIS_EXE makensis)
    if(MAKENSIS_EXE)
        set(CPACK_GENERATOR "NSIS")
    else()
        set(CPACK_GENERATOR "ZIP")
    endif()
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT_NAME}.exe")
    set(CPACK_NSIS_DISPLAY_NAME "Unnamed Strategy")
    set(CPACK_NSIS_PACKAGE_NAME "Unnamed Strategy")
    set(CPACK_PACKAGE_EXECUTABLES "${PROJECT_NAME}" "Unnamed Strategy")
    set(CPACK_CREATE_DESKTOP_LINKS "${PROJECT_NAME}")
endif()
include(CPack)

# ── Tests ─────────────────────────────────────────────────────────────────────
# Small, dependency-light checks for logic that is easy to break silently.
# Build with -DBUILD_TESTS=ON, then run: ctest --test-dir build --output-on-failure
option(BUILD_TESTS "Build unit tests" OFF)
if (BUILD_TESTS)
    enable_testing()
    # Hideout meta-progression: milestone XP must pay exactly once, and buying
    # an upgrade tier must complete its milestone. Both were broken before
    # 2026-07-25 and neither is visible without playing many full games.
    add_executable(test_hideout
        tests/test_hideout.cpp
        src/meta/HideoutDB.cpp
        src/core/DevLog.cpp)
    target_include_directories(test_hideout PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_hideout PRIVATE ${SQLITE_TARGET})
    add_test(NAME hideout_progression COMMAND test_hideout)
endif()
