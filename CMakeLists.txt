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
list(FILTER SOURCES EXCLUDE REGEX ".*/sim/fullgame_main\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX ".*/sim/FullGameSim\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX ".*/sim/SimDB\\.cpp$")

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party   # stb_image, glm go here
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

# ── Post-build: copy assets and create runtime dirs ───────────────────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/saves
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/maps
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts
)

# ── Standalone balance simulator (no SDL / ImGui / OpenGL) ───────────────────
set(SIM_SOURCES
    src/sim/sim_main.cpp
    src/sim/Simulator.cpp
    src/sim/ArmyBuilder.cpp
    src/town/BuildingRegistry.cpp
    src/core/DevLog.cpp
    src/combat/CombatEngine.cpp
    src/combat/CombatGrid.cpp
    src/combat/CombatUnit.cpp
    src/combat/DamageCalc.cpp
    src/world/HexGrid.cpp
    src/world/HexMap.cpp
    src/hero/Hero.cpp
    src/hero/Artifacts.cpp
)

add_executable(sim_test ${SIM_SOURCES})
target_include_directories(sim_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(sim_test PRIVATE cxx_std_20)

# ── Full AI-vs-AI game simulator (no SDL / ImGui / OpenGL) ───────────────────
set(FULLGAME_SOURCES
    src/sim/fullgame_main.cpp
    src/sim/FullGameSim.cpp
    src/sim/MCTSHero.cpp
    src/sim/SimDB.cpp
    src/core/DevLog.cpp
    src/combat/CombatEngine.cpp
    src/combat/CombatGrid.cpp
    src/combat/CombatUnit.cpp
    src/combat/DamageCalc.cpp
    src/world/HexGrid.cpp
    src/world/HexMap.cpp
    src/world/WorldGen.cpp
    src/hero/Hero.cpp
    src/hero/Artifacts.cpp
    src/hero/HeroClass.cpp
    src/town/Town.cpp
    src/town/BuildingRegistry.cpp
    src/ai/Pathfinder.cpp
)


add_executable(fullgame_sim ${FULLGAME_SOURCES})
target_include_directories(fullgame_sim PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fullgame_sim PRIVATE ${SQLITE_TARGET})
target_compile_features(fullgame_sim PRIVATE cxx_std_20)

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
