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
option(VOXOV_ENABLE_ANDROID_BACKEND "Compile Android platform backend skeleton" ON)
option(VOXOV_ENABLE_WEB_BACKEND "Compile Web platform backend skeleton" ON)
option(VOXOV_ENABLE_VULKAN "Enable Vulkan backend and shader toolchain requirements" ON)

if(ANDROID)
    message(STATUS "Configuring VOXOV for Android")

    set(VOXOV_ANDROID_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
    if(NOT EXISTS "${VOXOV_ANDROID_GLUE_DIR}/android_native_app_glue.c")
        message(FATAL_ERROR "android_native_app_glue.c not found at ${VOXOV_ANDROID_GLUE_DIR}. Check NDK setup.")
    endif()

    add_library(voxov_android SHARED
        src/game/android_main.cpp
        src/platform/android_platform.cpp
        src/platform/android/input_android.cpp
        ${VOXOV_ANDROID_GLUE_DIR}/android_native_app_glue.c
    )
    target_include_directories(voxov_android PRIVATE
        src
        dependencies/glm
        ${VOXOV_ANDROID_GLUE_DIR}
    )
    target_compile_definitions(voxov_android PRIVATE VOXOV_PLATFORM_ANDROID=1)

    find_library(android_lib android REQUIRED)
    find_library(log_lib log REQUIRED)
    target_link_libraries(voxov_android PRIVATE
        ${android_lib}
        ${log_lib}
    )

    set_target_properties(voxov_android PROPERTIES OUTPUT_NAME "voxov")
    install(TARGETS voxov_android
        LIBRARY DESTINATION lib
    )
    return()
endif()

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

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
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/glfw" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/enet" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/glm" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/fmt" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/spdlog" EXCLUDE_FROM_ALL)

# Find packages
if(VOXOV_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()
find_package(OpenGL REQUIRED)

set(VOXOV_SHADER_COMPILER "")
set(VOXOV_SHADER_COMPILER_ARGS "")
if(VOXOV_ENABLE_VULKAN)
    if(TARGET Vulkan::glslc)
        set(VOXOV_SHADER_COMPILER Vulkan::glslc)
        set(VOXOV_SHADER_COMPILER_ARGS "")
    else()
        find_program(VOXOV_GLSLANG_VALIDATOR glslangValidator)
        if(VOXOV_GLSLANG_VALIDATOR)
            set(VOXOV_SHADER_COMPILER "${VOXOV_GLSLANG_VALIDATOR}")
            set(VOXOV_SHADER_COMPILER_ARGS -V)
        else()
            message(FATAL_ERROR "No shader compiler found. Install glslc or glslangValidator.")
        endif()
    endif()
endif()

add_library(imgui STATIC
    dependencies/imgui/imgui.cpp
    dependencies/imgui/imgui_demo.cpp
    dependencies/imgui/imgui_draw.cpp
    dependencies/imgui/imgui_tables.cpp
    dependencies/imgui/imgui_widgets.cpp
    dependencies/imgui/backends/imgui_impl_glfw.cpp
    dependencies/imgui/backends/imgui_impl_opengl3.cpp
)
if(VOXOV_ENABLE_VULKAN)
    target_sources(imgui PRIVATE
        dependencies/imgui/backends/imgui_impl_vulkan.cpp
    )
endif()
target_include_directories(imgui PUBLIC
    dependencies/imgui
    dependencies/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
if(VOXOV_ENABLE_VULKAN)
    target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
endif()

add_library(engine_core STATIC
    src/engine_core/timing.cpp
    src/engine_core/jobs.cpp
    src/engine_core/memory.cpp
)
target_include_directories(engine_core PUBLIC src)
target_link_libraries(engine_core PUBLIC fmt::fmt spdlog::spdlog)

add_library(platform_desktop STATIC
    src/platform/platform.cpp
    src/platform/desktop/input_desktop.cpp
    src/platform/android/input_android.cpp
)
if(VOXOV_ENABLE_ANDROID_BACKEND)
    target_sources(platform_desktop PRIVATE src/platform/android_platform.cpp)
endif()
if(VOXOV_ENABLE_WEB_BACKEND)
    target_sources(platform_desktop PRIVATE src/platform/web_platform.cpp)
endif()
target_include_directories(platform_desktop PUBLIC src)
target_link_libraries(platform_desktop PUBLIC glfw glm::glm)

add_library(engine_math STATIC
    src/engine_math/camera.cpp
)
target_include_directories(engine_math PUBLIC src)
target_link_libraries(engine_math PUBLIC glm::glm)

add_library(engine_world STATIC
    src/engine_world/voxel_chunk.cpp
    src/engine_world/physics/voxel_collision.cpp
)
target_include_directories(engine_world PUBLIC src)
target_link_libraries(engine_world PUBLIC engine_math)

add_library(engine_render STATIC
    src/engine_render/renderer.cpp
    src/engine_render/gl_renderer.cpp
    src/engine_render/debug_text.cpp
    src/engine_render/debug_draw/debug_draw.cpp
)
if(VOXOV_ENABLE_VULKAN)
    target_sources(engine_render PRIVATE src/engine_render/vulkan_renderer.cpp)
endif()
target_include_directories(engine_render PUBLIC src)
target_include_directories(engine_render PUBLIC ${CMAKE_SOURCE_DIR}/dependencies/VulkanMemoryAllocator/include)
target_link_libraries(engine_render PUBLIC OpenGL::GL glfw glm::glm fmt::fmt spdlog::spdlog imgui)
if(VOXOV_ENABLE_VULKAN)
    target_link_libraries(engine_render PUBLIC Vulkan::Vulkan)
    target_compile_definitions(engine_render PUBLIC VOXOV_ENABLE_VULKAN=1)
else()
    target_compile_definitions(engine_render PUBLIC VOXOV_ENABLE_VULKAN=0)
endif()

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
    src/engine_gameplay/player/player_controller.cpp
    src/engine_ui/gui_menu.cpp
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC engine_core engine_render engine_physics engine_net engine_math engine_world platform_desktop)

add_executable(voxov
    src/game/main.cpp
)
target_include_directories(voxov PRIVATE src)
target_link_libraries(voxov PRIVATE
    engine
    glfw
    OpenGL::GL
)
if(VOXOV_ENABLE_VULKAN)
    target_link_libraries(voxov PRIVATE Vulkan::Vulkan)
endif()

# Shader compilation for game
if(VOXOV_ENABLE_VULKAN)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
        COMMAND ${VOXOV_SHADER_COMPILER} ${VOXOV_SHADER_COMPILER_ARGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.vert -o ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.vert
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
        COMMAND ${VOXOV_SHADER_COMPILER} ${VOXOV_SHADER_COMPILER_ARGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.frag -o ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/triangle.frag
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stylized_lighting.frag.spv
        COMMAND ${VOXOV_SHADER_COMPILER} ${VOXOV_SHADER_COMPILER_ARGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/stylized_lighting.frag -o ${CMAKE_CURRENT_BINARY_DIR}/stylized_lighting.frag.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/stylized_lighting.frag
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/palette_dither.frag.spv
        COMMAND ${VOXOV_SHADER_COMPILER} ${VOXOV_SHADER_COMPILER_ARGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/palette_dither.frag -o ${CMAKE_CURRENT_BINARY_DIR}/palette_dither.frag.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/palette_dither.frag
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv
        COMMAND ${VOXOV_SHADER_COMPILER} ${VOXOV_SHADER_COMPILER_ARGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_render/shaders/fullscreen.vert -o ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv
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
endif()

add_executable(voxov_asset_cooker
    src/tools/asset_cooker.cpp
    src/engine_assets/cooked_asset.cpp
)
target_include_directories(voxov_asset_cooker PRIVATE src)
target_link_libraries(voxov_asset_cooker PRIVATE fmt::fmt)

# Installation
install(TARGETS voxov
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(VOXOV_BUILD_TESTS)
    enable_testing()
    add_executable(voxov_tests
        src/tests/test_main.cpp
    )
    target_include_directories(voxov_tests PRIVATE src)
    target_link_libraries(voxov_tests PRIVATE engine engine_math engine_world engine_net)
    add_test(NAME voxov_tests COMMAND voxov_tests)
endif()
