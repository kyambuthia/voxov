cmake_minimum_required(VERSION 3.16.0)

project(voxov
    VERSION 1.0.0
    DESCRIPTION "Voxov - An OpenGL-first application"
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
option(VOXOV_BUILD_LEGACY_DEMOS "Build legacy SDL/Vulkan demo entrypoints" OFF)
option(VOXOV_ENABLE_ASSERTIONS "Enable assertions in release builds" ON)
option(VOXOV_ENABLE_ANDROID_BACKEND "Compile Android platform backend skeleton" ON)
option(VOXOV_ENABLE_WEB_BACKEND "Compile Web platform backend skeleton" ON)
option(VOXOV_ENABLE_IOS_BACKEND "Compile iOS platform backend scaffold" OFF)
option(VOXOV_ENABLE_CONSOLE_BACKEND "Compile console platform backend scaffold" OFF)
option(VOXOV_ENABLE_XR "Compile XR session scaffold" OFF)

if(ANDROID)
    message(STATUS "Configuring VOXOV for Android")

    set(ENET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/enet" EXCLUDE_FROM_ALL)
    add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/fmt" EXCLUDE_FROM_ALL)
    add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/spdlog" EXCLUDE_FROM_ALL)

    set(VOXOV_ANDROID_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
    if(NOT EXISTS "${VOXOV_ANDROID_GLUE_DIR}/android_native_app_glue.c")
        message(FATAL_ERROR "android_native_app_glue.c not found at ${VOXOV_ANDROID_GLUE_DIR}. Check NDK setup.")
    endif()

    add_library(voxov_android SHARED
        src/game/android_main.cpp
        src/engine_runtime/runtime_session_controller.cpp
        src/platform/android_platform.cpp
        src/platform/android/input_android.cpp
        src/engine_assets/cgltf_impl.cpp
        src/engine_assets/skinned_model.cpp
        src/engine_gameplay/animation/animation_runtime.cpp
        src/engine_gameplay/animation/player_animation_graph.cpp
        src/engine_gameplay/animation/player_animation_types.cpp
        src/engine_gameplay/animation/skeletal_animator.cpp
        src/engine_gameplay/player/player_controller.cpp
        src/engine_gameplay/player/player_visuals.cpp
        src/engine_ui/gui_menu.cpp
        src/engine_audio/ui_audio.cpp
        src/engine_math/camera.cpp
        src/engine_render/debug_text.cpp
        src/engine_render/debug_draw/debug_draw.cpp
        src/engine_world/world_gen.cpp
        src/engine_world/voxel_chunk.cpp
        src/engine_world/physics/voxel_collision.cpp
        src/engine_net/net_client.cpp
        src/engine_net/net_server.cpp
        src/engine_net/lan_discovery.cpp
        ${VOXOV_ANDROID_GLUE_DIR}/android_native_app_glue.c
    )
    target_include_directories(voxov_android PRIVATE
        src
        dependencies/glm
        dependencies/cgltf
        dependencies/miniaudio
        dependencies/enet/include
        ${VOXOV_ANDROID_GLUE_DIR}
    )
    target_compile_definitions(voxov_android PRIVATE
        VOXOV_PLATFORM_ANDROID=1
        GLM_ENABLE_EXPERIMENTAL
    )

    find_library(android_lib android REQUIRED)
    find_library(log_lib log REQUIRED)
    find_library(egl_lib EGL REQUIRED)
    find_library(glesv3_lib GLESv3 REQUIRED)
    target_link_libraries(voxov_android PRIVATE
        ${android_lib}
        ${log_lib}
        ${egl_lib}
        ${glesv3_lib}
        enet
        fmt::fmt
        spdlog::spdlog
    )

    set_target_properties(voxov_android PROPERTIES OUTPUT_NAME "voxov")
    install(TARGETS voxov_android
        LIBRARY DESTINATION lib
    )
    return()
endif()

if(EMSCRIPTEN)
    message(STATUS "Configuring VOXOV for Web (Emscripten)")

    add_executable(voxov_web
        src/game/web_main.cpp
        src/engine_runtime/runtime_session_controller.cpp
        src/platform/web_platform.cpp
        src/engine_ui/gui_menu.cpp
    )
    target_include_directories(voxov_web PRIVATE src dependencies/glm)
    target_compile_definitions(voxov_web PRIVATE VOXOV_PLATFORM_WEB=1)
    target_link_options(voxov_web PRIVATE
        "-sUSE_WEBGL2=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sWASM=1"
    )
    set_target_properties(voxov_web PROPERTIES OUTPUT_NAME "voxov_web" SUFFIX ".html")
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
# Prevent MinGW/GCC false-positive warnings in Jolt from failing CI due to -Werror.
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)

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
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    dependencies/imgui/imgui.cpp
    dependencies/imgui/imgui_demo.cpp
    dependencies/imgui/imgui_draw.cpp
    dependencies/imgui/imgui_tables.cpp
    dependencies/imgui/imgui_widgets.cpp
    dependencies/imgui/backends/imgui_impl_glfw.cpp
    dependencies/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    dependencies/imgui
    dependencies/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

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
)
target_include_directories(platform_desktop PUBLIC src)
target_link_libraries(platform_desktop PUBLIC glfw glm::glm)

add_library(engine_math STATIC
    src/engine_math/camera.cpp
)
target_include_directories(engine_math PUBLIC src)
target_link_libraries(engine_math PUBLIC glm::glm)

if(VOXOV_ENABLE_XR)
    add_library(engine_xr STATIC
        src/engine_xr/xr_session.cpp
    )
    target_include_directories(engine_xr PUBLIC src)
    target_link_libraries(engine_xr PUBLIC glm::glm)
endif()

add_library(engine_world STATIC
    src/engine_world/voxel_chunk.cpp
    src/engine_world/world_gen.cpp
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
target_include_directories(engine_render PUBLIC src)
target_link_libraries(engine_render PUBLIC OpenGL::GL glfw glm::glm fmt::fmt spdlog::spdlog imgui)

add_library(engine_physics STATIC
    src/engine_physics/avbd_solver.cpp
    src/engine_physics/jolt_solver.cpp
    src/engine_physics/vehicle/aircraft_controller.cpp
    src/engine_physics/vehicle/vehicle_damage_model.cpp
    src/engine_physics/vehicle/vehicle_sandbox_scene.cpp
    src/engine_physics/vehicle/ground_vehicle_controller.cpp
    src/engine_physics/vehicle/vehicle_drivetrain.cpp
    src/engine_physics/vehicle/vehicle_foundation.cpp
    src/engine_physics/vehicle/voxel_vehicle_builder.cpp
    src/engine_physics/voxel/voxel_physics_bridge.cpp
    src/engine_physics/physics_world.cpp
)
target_include_directories(engine_physics PUBLIC src ${CMAKE_SOURCE_DIR}/dependencies/JoltPhysics)
target_link_libraries(engine_physics PUBLIC Jolt fmt::fmt spdlog::spdlog)

add_library(engine_net STATIC
    src/engine_net/net_client.cpp
    src/engine_net/net_server.cpp
    src/engine_net/lan_discovery.cpp
    src/engine_gameplay/animation/player_animation_graph.cpp
    src/engine_gameplay/animation/player_animation_types.cpp
    src/engine_gameplay/player/player_controller.cpp
)
target_include_directories(engine_net PUBLIC src ${CMAKE_SOURCE_DIR}/dependencies/enet/include)
target_link_libraries(engine_net PUBLIC enet fmt::fmt spdlog::spdlog engine_world engine_math glm::glm)
if(WIN32)
    target_link_libraries(engine_net PUBLIC ws2_32)
endif()

add_library(engine STATIC
    src/engine/engine.cpp
    src/engine_runtime/runtime_session_controller.cpp
    src/engine_assets/cgltf_impl.cpp
    src/engine_assets/skinned_model.cpp
    src/engine_audio/ui_audio.cpp
    src/engine_gameplay/animation/animation_runtime.cpp
    src/engine_gameplay/animation/player_animation_graph.cpp
    src/engine_gameplay/animation/player_animation_types.cpp
    src/engine_gameplay/animation/skeletal_animator.cpp
    src/engine_gameplay/minigames/minigames.cpp
    src/engine_gameplay/player/player_controller.cpp
    src/engine_gameplay/player/player_visuals.cpp
    src/engine_ui/gui_menu.cpp
)
target_include_directories(engine PUBLIC src dependencies/miniaudio dependencies/cgltf)
target_link_libraries(engine PUBLIC engine_core engine_render engine_physics engine_net engine_math engine_world platform_desktop)
if(VOXOV_ENABLE_XR)
    target_link_libraries(engine PUBLIC engine_xr)
    target_compile_definitions(engine PUBLIC VOXOV_ENABLE_XR=1)
else()
    target_compile_definitions(engine PUBLIC VOXOV_ENABLE_XR=0)
endif()

set(VOXOV_PLATFORM_FUTURE_SOURCES "")
if(VOXOV_ENABLE_IOS_BACKEND)
    list(APPEND VOXOV_PLATFORM_FUTURE_SOURCES
        src/platform/ios_platform.cpp
    )
endif()
if(VOXOV_ENABLE_CONSOLE_BACKEND)
    list(APPEND VOXOV_PLATFORM_FUTURE_SOURCES
        src/platform/console_platform.cpp
    )
endif()
if(VOXOV_PLATFORM_FUTURE_SOURCES)
    add_library(platform_future STATIC ${VOXOV_PLATFORM_FUTURE_SOURCES})
    target_include_directories(platform_future PUBLIC src)
endif()

add_executable(voxov
    src/game/main.cpp
)
target_include_directories(voxov PRIVATE src)
target_link_libraries(voxov PRIVATE
    engine
    glfw
    OpenGL::GL
)

add_executable(voxov_asset_cooker
    src/tools/asset_cooker.cpp
    src/engine_assets/cooked_asset.cpp
)
target_include_directories(voxov_asset_cooker PRIVATE src)
target_link_libraries(voxov_asset_cooker PRIVATE fmt::fmt)

# MinGW builds require GCC runtime DLLs next to the executable for startup.
if(MINGW)
    set(VOXOV_MINGW_RUNTIME_DLLS "")
    set(VOXOV_MINGW_RUNTIME_DLL_NAMES
        libgcc_s_seh-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
    )

    foreach(runtime_dll_name IN LISTS VOXOV_MINGW_RUNTIME_DLL_NAMES)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=${runtime_dll_name}
            OUTPUT_VARIABLE runtime_dll_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(runtime_dll_path AND NOT runtime_dll_path STREQUAL runtime_dll_name)
            list(APPEND VOXOV_MINGW_RUNTIME_DLLS "${runtime_dll_path}")
        else()
            message(WARNING "Could not locate MinGW runtime DLL: ${runtime_dll_name}")
        endif()
    endforeach()

    if(VOXOV_MINGW_RUNTIME_DLLS)
        add_custom_command(TARGET voxov POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${VOXOV_MINGW_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:voxov>
        )
    endif()
endif()

# Installation
install(TARGETS voxov
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(MINGW AND VOXOV_MINGW_RUNTIME_DLLS)
    install(FILES ${VOXOV_MINGW_RUNTIME_DLLS}
        DESTINATION bin
    )
endif()

# Runtime model assets are required for skinned character rendering.
install(DIRECTORY assets/models/player/
    DESTINATION bin/assets/models/player
    FILES_MATCHING PATTERN "*.glb" PATTERN "*.md" PATTERN "LICENSE*"
)

install(DIRECTORY assets/fonts/
    DESTINATION bin/assets/fonts
    FILES_MATCHING PATTERN "*.ttf" PATTERN "*.otf" PATTERN "*.md" PATTERN "LICENSE*"
)

if(VOXOV_BUILD_TESTS)
    enable_testing()
    add_executable(voxov_tests
        src/tests/test_main.cpp
        src/platform/android_platform.cpp
        src/platform/web_platform.cpp
    )
    target_include_directories(voxov_tests PRIVATE src)
    target_link_libraries(voxov_tests PRIVATE engine engine_math engine_world engine_net)
    add_test(NAME voxov_tests COMMAND voxov_tests)

    add_executable(voxov_net_stress
        src/tests/net_stress.cpp
    )
    target_include_directories(voxov_net_stress PRIVATE src)
    target_link_libraries(voxov_net_stress PRIVATE engine_net)
    add_test(NAME voxov_net_stress COMMAND voxov_net_stress)
endif()

if(VOXOV_BUILD_LEGACY_DEMOS)
    add_custom_target(voxov_legacy_sources
        SOURCES
            src/main.cpp
            src/game.cpp
            src/renderer/vulkan_app.cpp
            src/world/planet_world.cpp
            src/engine_assets/static_model.cpp
    )
endif()
