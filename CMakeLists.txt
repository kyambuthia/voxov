cmake_minimum_required(VERSION 3.16.0)

project(voxov
    VERSION 1.0.0
    DESCRIPTION "Voxov - A Vulkan-based application"
    LANGUAGES C CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Build options
option(VOXOV_BUILD_TESTS "Build tests" OFF)
option(VOXOV_BUILD_EXAMPLES "Build examples" OFF)
option(VOXOV_ENABLE_ASSERTIONS "Enable assertions in release builds" ON)

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(JPH_GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)

# Dependencies
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/JoltPhysics/Build" EXCLUDE_FROM_ALL)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/glfw" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/enet" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/fmt" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/spdlog" EXCLUDE_FROM_ALL)

# Find packages
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)
find_package(Vulkan COMPONENTS glslc REQUIRED)

add_library(engine_core STATIC
    src/engine_core/timing.cpp
    src/engine_core/jobs.cpp
    src/engine_core/memory.cpp
)
target_include_directories(engine_core PUBLIC src)
target_link_libraries(engine_core PUBLIC fmt::fmt spdlog::spdlog)

add_library(engine_render STATIC
    src/engine_render/renderer.cpp
    src/engine_render/vulkan_renderer.cpp
)
target_include_directories(engine_render PUBLIC src)
target_include_directories(engine_render PUBLIC ${CMAKE_SOURCE_DIR}/dependencies/VulkanMemoryAllocator/include)
target_link_libraries(engine_render PUBLIC Vulkan::Vulkan glfw fmt::fmt spdlog::spdlog)

add_library(engine_physics STATIC
    src/engine_physics/physics_world.cpp
)
target_include_directories(engine_physics PUBLIC src ${CMAKE_SOURCE_DIR}/dependencies/JoltPhysics)
target_link_libraries(engine_physics PUBLIC Jolt fmt::fmt spdlog::spdlog)

add_library(engine_net STATIC
    src/engine_net/net_client.cpp
    src/engine_net/net_server.cpp
)
target_include_directories(engine_net PUBLIC src ${CMAKE_SOURCE_DIR}/dependencies/enet/include)
target_link_libraries(engine_net PUBLIC enet fmt::fmt spdlog::spdlog)

add_library(engine STATIC
    src/engine/engine.cpp
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC engine_core engine_render engine_physics engine_net)

add_executable(voxov
    src/game/main.cpp
)
target_include_directories(voxov PRIVATE src)
target_link_libraries(voxov PRIVATE
    engine
    glfw
    Vulkan::Vulkan
)

# Shader compilation for game
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.vert -o ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.vert
    VERBATIM
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.frag -o ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.frag
    VERBATIM
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stylized_lighting.frag.spv
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/stylized_lighting.frag -o ${CMAKE_CURRENT_BINARY_DIR}/stylized_lighting.frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/stylized_lighting.frag
    VERBATIM
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/palette_dither.frag.spv
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/palette_dither.frag -o ${CMAKE_CURRENT_BINARY_DIR}/palette_dither.frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/palette_dither.frag
    VERBATIM
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/fullscreen.vert -o ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/fullscreen.vert
    VERBATIM
)
add_custom_target(game_shaders ALL
    DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
    ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
    ${CMAKE_CURRENT_BINARY_DIR}/stylized_lighting.frag.spv
    ${CMAKE_CURRENT_BINARY_DIR}/palette_dither.frag.spv
    ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv
)
add_dependencies(voxov game_shaders)


# Installation
install(TARGETS voxov
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
