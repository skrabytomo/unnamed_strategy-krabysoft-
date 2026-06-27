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
find_package(Lua 5.4 REQUIRED)
find_package(nlohmann_json REQUIRED)

# ── ImGui (system package) ────────────────────────────────────────────────────
add_library(imgui_lib INTERFACE)
target_include_directories(imgui_lib INTERFACE
    /usr/include/imgui
    /usr/include/imgui/backends
)
target_link_libraries(imgui_lib INTERFACE
    /usr/lib/x86_64-linux-gnu/libimgui.a
    /usr/lib/x86_64-linux-gnu/libstb.a
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    GL
)

# ── Game sources ──────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
)
# Exclude the simulator's own main — it has its own executable target below
list(FILTER SOURCES EXCLUDE REGEX ".*/sim/sim_main\\.cpp$")

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
    SQLite::SQLite3
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
