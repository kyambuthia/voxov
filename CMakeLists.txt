cmake_minimum_required(VERSION 3.16.0)

project(voxov
    VERSION 1.0.0
    DESCRIPTION "Voxov — sokol-based game engine"
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

# ── Build options ──────────────────────────────────────────────────────
option(VOXOV_BUILD_TESTS "Build tests" OFF)
option(VOXOV_BUILD_EXAMPLES "Build examples" OFF)
option(VOXOV_ENABLE_ASSERTIONS "Enable assertions in release builds" ON)
option(VOXOV_ENABLE_ANDROID_BACKEND "Compile Android platform backend skeleton" ON)
option(VOXOV_ENABLE_WEB_BACKEND "Compile Web platform backend skeleton" ON)
option(VOXOV_ENABLE_IOS_BACKEND "Compile iOS platform backend scaffold" OFF)
option(VOXOV_ENABLE_CONSOLE_BACKEND "Compile console platform backend scaffold" OFF)
option(VOXOV_ENABLE_XR "Compile XR session scaffold" OFF)

# ── Sokol headers (header-only; no subdir build needed) ────────────────
set(VOXOV_SOKOL_DIR "${CMAKE_SOURCE_DIR}/dependencies/sokol")
set(VOXOV_SOKOL_UTIL_DIR "${VOXOV_SOKOL_DIR}/util")

# ── Compiler flags ─────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

# ── Core dependency settings ───────────────────────────────────────────
set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(JPH_GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SSE4_1 OFF CACHE BOOL "" FORCE)
set(USE_SSE4_2 OFF CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)

set(ENET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# ── Dependencies (always needed) ───────────────────────────────────────
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/enet" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/glm" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/fmt" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/spdlog" EXCLUDE_FROM_ALL)

# ── Platform backend ───────────────────────────────────────────────────
if(ANDROID)
    message(STATUS "Configuring VOXOV for Android")
    set(VOXOV_ANDROID_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
    if(NOT EXISTS "${VOXOV_ANDROID_GLUE_DIR}/android_native_app_glue.c")
        message(FATAL_ERROR "android_native_app_glue.c not found at ${VOXOV_ANDROID_GLUE_DIR}")
    endif()
    add_compile_definitions(VOXOV_PLATFORM_ANDROID=1)
    add_compile_definitions(SOKOL_GLES3)
elseif(EMSCRIPTEN)
    message(STATUS "Configuring VOXOV for Web (Emscripten)")
    add_compile_definitions(VOXOV_PLATFORM_WEB=1)
    add_compile_definitions(SOKOL_GLES3)
elseif(NOT EMSCRIPTEN)
    message(STATUS "Platform backend: sokol_app + sokol_gfx")
    if(APPLE)
        set(VOXOV_SOKOL_BACKEND SOKOL_METAL)
    elseif(WIN32)
        set(VOXOV_SOKOL_BACKEND SOKOL_D3D11)
    else()
        set(VOXOV_SOKOL_BACKEND SOKOL_GLCORE)
        find_package(OpenGL REQUIRED)
    endif()
    add_compile_definitions(${VOXOV_SOKOL_BACKEND})
endif()
add_compile_definitions(VOXOV_HAS_RENDER_SOKOL=1)

add_library(sokol_gfx_impl STATIC
    src/platform/sokol/sokol_gfx_impl.cpp
)
target_include_directories(sokol_gfx_impl PUBLIC
    ${VOXOV_SOKOL_DIR}
)
if(ANDROID)
    target_compile_definitions(sokol_gfx_impl PUBLIC SOKOL_GLES3 VOXOV_PLATFORM_ANDROID=1)
elseif(EMSCRIPTEN)
    target_compile_definitions(sokol_gfx_impl PUBLIC SOKOL_GLES3 VOXOV_PLATFORM_WEB=1)
endif()

if(NOT ANDROID)
    # Platform library (sokol_app/audio/glue)
    add_library(platform_desktop STATIC
        src/platform/sokol/sokol_impl_desktop.cpp
        src/platform/sokol/sokol_platform.cpp
        src/platform/desktop/input_desktop.cpp
    )
    target_include_directories(platform_desktop PUBLIC
        src
        ${VOXOV_SOKOL_DIR}
        ${VOXOV_SOKOL_UTIL_DIR}
    )
    target_link_libraries(platform_desktop PUBLIC sokol_gfx_impl glm::glm)
    if(EMSCRIPTEN)
        target_compile_definitions(platform_desktop PUBLIC SOKOL_GLES3 VOXOV_PLATFORM_WEB=1)
    endif()
    if(WIN32)
        target_link_libraries(platform_desktop PUBLIC d3d11 dxgi)
    elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
        target_link_libraries(platform_desktop PUBLIC
            OpenGL::GL X11 Xi Xcursor m dl asound
        )
    endif()
endif()

# ── Core systems ───────────────────────────────────────────────────────
add_library(engine_core STATIC
    src/engine_core/timing.cpp
    src/engine_core/jobs.cpp
    src/engine_core/memory.cpp
    src/engine_core/cvar.cpp
)
target_include_directories(engine_core PUBLIC src)
target_link_libraries(engine_core PUBLIC fmt::fmt spdlog::spdlog)

add_library(engine_math STATIC
    src/engine_math/camera.cpp
)
target_include_directories(engine_math PUBLIC src)
target_link_libraries(engine_math PUBLIC glm::glm)

add_library(engine_events STATIC
    src/engine_events/event_bus.cpp
)
target_include_directories(engine_events PUBLIC src)
target_link_libraries(engine_events PUBLIC glm::glm)

add_library(engine_net_proto INTERFACE)
target_include_directories(engine_net_proto INTERFACE src)

if(VOXOV_ENABLE_XR)
    add_library(engine_xr STATIC src/engine_xr/xr_session.cpp)
    target_include_directories(engine_xr PUBLIC src)
    target_link_libraries(engine_xr PUBLIC glm::glm)
endif()

add_library(engine_world STATIC
    src/engine_world/planet.cpp
    src/engine_world/voxel_chunk.cpp
    src/engine_world/world_gen.cpp
    src/engine_world/flat_world_streamer.cpp
    src/engine_world/physics/voxel_collision.cpp
)
target_include_directories(engine_world PUBLIC src)
target_link_libraries(engine_world PUBLIC engine_math)

# ── Render backends ────────────────────────────────────────────────────
add_library(engine_render_core STATIC
    src/engine_render/renderer.cpp
    src/engine_render/debug_text.cpp
    src/engine_render/debug_draw/debug_draw.cpp
)
target_include_directories(engine_render_core PUBLIC src)
target_include_directories(engine_render_core PUBLIC
    ${VOXOV_SOKOL_DIR}
    ${VOXOV_SOKOL_UTIL_DIR}
)
target_link_libraries(engine_render_core PUBLIC glm::glm fmt::fmt spdlog::spdlog)

add_library(engine_render_sokol STATIC
    src/engine_render/sokol_renderer.cpp
)
target_include_directories(engine_render_sokol PUBLIC
    src
    ${VOXOV_SOKOL_DIR}
    ${VOXOV_SOKOL_UTIL_DIR}
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(engine_render_sokol PRIVATE -fpermissive)
endif()
target_link_libraries(engine_render_sokol PUBLIC glm::glm sokol_gfx_impl)
target_compile_definitions(engine_render_sokol PUBLIC VOXOV_HAS_RENDER_SOKOL=1)
if(ANDROID)
    target_compile_definitions(engine_render_sokol PUBLIC SOKOL_GLES3 VOXOV_PLATFORM_ANDROID=1)
elseif(EMSCRIPTEN)
    target_compile_definitions(engine_render_sokol PUBLIC SOKOL_GLES3 VOXOV_PLATFORM_WEB=1)
endif()
target_compile_definitions(engine_render_core PUBLIC VOXOV_HAS_RENDER_SOKOL=1)
target_link_libraries(engine_render_core PRIVATE engine_render_sokol)

# ── Remaining subsystems ───────────────────────────────────────────────
add_library(engine_physics STATIC
    src/engine_physics/avbd_solver.cpp
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
target_include_directories(engine_physics PUBLIC src)
target_link_libraries(engine_physics PUBLIC fmt::fmt spdlog::spdlog glm::glm)

add_library(engine_gameplay STATIC
    src/engine_gameplay/animation/animation_runtime.cpp
    src/engine_gameplay/animation/player_animation_graph.cpp
    src/engine_gameplay/animation/player_animation_types.cpp
    src/engine_gameplay/animation/skeletal_animator.cpp
    src/engine_gameplay/minigames/minigames.cpp
    src/engine_gameplay/player/player_controller.cpp
    src/engine_gameplay/player/player_visuals.cpp
)
target_include_directories(engine_gameplay PUBLIC src)
target_link_libraries(engine_gameplay PUBLIC engine_render_core engine_world engine_math glm::glm)

add_library(engine_assets STATIC
    src/engine_assets/cgltf_impl.cpp
    src/engine_assets/skinned_model.cpp
)
target_include_directories(engine_assets PUBLIC src dependencies/cgltf)
target_link_libraries(engine_assets PUBLIC engine_gameplay engine_render_core glm::glm)

add_library(engine_audio STATIC
    src/engine_audio/ui_audio.cpp
)
target_include_directories(engine_audio PUBLIC src)
target_include_directories(engine_audio PUBLIC ${VOXOV_SOKOL_DIR})

add_library(engine_ui STATIC
    src/engine_ui/gui_menu.cpp
)
target_include_directories(engine_ui PUBLIC src)
target_link_libraries(engine_ui PUBLIC glm::glm)

add_library(engine_runtime STATIC
    src/engine_runtime/runtime_game_session.cpp
    src/engine_runtime/runtime_session_controller.cpp
    src/engine_runtime/runtime_world_state.cpp
)
target_include_directories(engine_runtime PUBLIC src)
target_link_libraries(engine_runtime PUBLIC engine_core engine_net engine_net_proto engine_render_core engine_ui engine_world)

add_library(engine_presentation STATIC
    src/engine_presentation/debug_scene_builder.cpp
    src/engine_presentation/hud_composer.cpp
)
target_include_directories(engine_presentation PUBLIC src)
target_link_libraries(engine_presentation PUBLIC
    engine_assets engine_gameplay engine_net engine_net_proto
    engine_physics engine_render_core engine_ui engine_world
)

add_library(engine_server STATIC
    src/engine_server/server_session.cpp
)
target_include_directories(engine_server PUBLIC src)
target_link_libraries(engine_server PUBLIC engine_gameplay engine_net_proto engine_world engine_math glm::glm)

add_library(engine_net_transport STATIC
    src/engine_net/net_client.cpp
    src/engine_net/net_server.cpp
)
target_include_directories(engine_net_transport PUBLIC src ${CMAKE_SOURCE_DIR}/dependencies/enet/include)
target_link_libraries(engine_net_transport PUBLIC enet fmt::fmt spdlog::spdlog engine_net_proto engine_server engine_world engine_math glm::glm)
if(WIN32)
    target_link_libraries(engine_net_transport PUBLIC ws2_32)
endif()

add_library(engine_net_discovery STATIC
    src/engine_net/lan_discovery.cpp
)
target_include_directories(engine_net_discovery PUBLIC src)
target_link_libraries(engine_net_discovery PUBLIC fmt::fmt spdlog::spdlog)
if(WIN32)
    target_link_libraries(engine_net_discovery PUBLIC ws2_32)
endif()

add_library(engine_net INTERFACE)
target_include_directories(engine_net INTERFACE src ${CMAKE_SOURCE_DIR}/dependencies/enet/include)
target_link_libraries(engine_net INTERFACE engine_net_discovery engine_net_transport)

# ── Engine umbrella library ────────────────────────────────────────────
add_library(engine STATIC
    src/engine/engine.cpp
    src/platform/platform_services.cpp
)
target_include_directories(engine PUBLIC src dependencies/cgltf)
target_link_libraries(engine PUBLIC
    engine_assets
    engine_audio
    engine_core
    engine_events
    engine_gameplay
    engine_math
    engine_net
    engine_physics
    engine_presentation
    engine_render_core
    engine_runtime
    engine_ui
    engine_world
)
if(NOT ANDROID)
    target_link_libraries(engine PUBLIC platform_desktop)
endif()
if(VOXOV_ENABLE_XR)
    target_link_libraries(engine PUBLIC engine_xr)
    target_compile_definitions(engine PUBLIC VOXOV_ENABLE_XR=1)
else()
    target_compile_definitions(engine PUBLIC VOXOV_ENABLE_XR=0)
endif()

# ── Game runtime ───────────────────────────────────────────────────────
add_library(game_runtime STATIC
    src/game/game_runtime.cpp
    src/game/runtime_session_flow.cpp
)
if(ANDROID)
    target_sources(game_runtime PRIVATE src/game/android_runtime_adapter.cpp src/platform/android/input_android.cpp)
elseif(EMSCRIPTEN)
    target_sources(game_runtime PRIVATE src/game/web_runtime_adapter.cpp)
else()
    target_sources(game_runtime PRIVATE src/game/desktop_runtime_adapter.cpp)
endif()
target_include_directories(game_runtime PUBLIC src)
target_link_libraries(game_runtime PUBLIC engine)
if(NOT ANDROID)
    target_link_libraries(game_runtime PUBLIC platform_desktop)
endif()

if(ANDROID)
    add_library(voxov_android SHARED
        src/game/android_main.cpp
        ${VOXOV_ANDROID_GLUE_DIR}/android_native_app_glue.c
    )
    target_include_directories(voxov_android PRIVATE
        src
        dependencies/glm
        dependencies/cgltf
        dependencies/enet/include
        ${VOXOV_ANDROID_GLUE_DIR}
        ${VOXOV_SOKOL_DIR}
        ${VOXOV_SOKOL_UTIL_DIR}
    )
    target_compile_definitions(voxov_android PRIVATE
        VOXOV_PLATFORM_ANDROID=1
        SOKOL_GLES3
        GLM_ENABLE_EXPERIMENTAL
    )

    find_library(android_lib android REQUIRED)
    find_library(log_lib log REQUIRED)
    find_library(egl_lib EGL REQUIRED)
    find_library(glesv3_lib GLESv3 REQUIRED)
    target_link_libraries(voxov_android PRIVATE
        game_runtime
        engine
        sokol_gfx_impl
        ${android_lib}
        ${log_lib}
        ${egl_lib}
        ${glesv3_lib}
        enet
        fmt::fmt
        spdlog::spdlog
    )
    set_target_properties(voxov_android PROPERTIES OUTPUT_NAME "voxov")
    install(TARGETS voxov_android LIBRARY DESTINATION lib)
    return()
endif()

if(EMSCRIPTEN)
    add_executable(voxov_web
        src/game/web_main.cpp
    )
    target_include_directories(voxov_web PRIVATE
        src
        ${VOXOV_SOKOL_DIR}
        ${VOXOV_SOKOL_UTIL_DIR}
    )
    target_compile_definitions(voxov_web PRIVATE
        VOXOV_PLATFORM_WEB=1
        SOKOL_GLES3
        GLM_ENABLE_EXPERIMENTAL
        SOKOL_DEBUG=0
    )
    target_link_libraries(voxov_web PRIVATE
        game_runtime
        engine
        sokol_gfx_impl
        fmt::fmt
        spdlog::spdlog
    )
    target_link_options(voxov_web PRIVATE
        "-sUSE_WEBGL2=1" "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1" "-sINITIAL_MEMORY=134217728"
        "-sSTACK_SIZE=4194304" "-sWASM=1"
        "-sFORCE_FILESYSTEM=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING=1"
        "--preload-file=${CMAKE_SOURCE_DIR}/assets@/assets"
        "--shell-file=${CMAKE_SOURCE_DIR}/assets/web/shell.html"
    )
    set_target_properties(voxov_web PROPERTIES OUTPUT_NAME "voxov_web" SUFFIX ".html")
    return()
endif()

# ── Platform future scaffolds ─────────────────────────────────────────
set(VOXOV_PLATFORM_FUTURE_SOURCES "")
if(VOXOV_ENABLE_IOS_BACKEND)
    list(APPEND VOXOV_PLATFORM_FUTURE_SOURCES src/platform/ios_platform.cpp)
endif()
if(VOXOV_ENABLE_CONSOLE_BACKEND)
    list(APPEND VOXOV_PLATFORM_FUTURE_SOURCES src/platform/console_platform.cpp)
endif()
if(VOXOV_PLATFORM_FUTURE_SOURCES)
    add_library(platform_future STATIC ${VOXOV_PLATFORM_FUTURE_SOURCES})
    target_include_directories(platform_future PUBLIC src)
endif()

# ── Main executable ────────────────────────────────────────────────────
add_executable(voxov
    src/game/main_sokol.cpp
)
target_include_directories(voxov PRIVATE
    src
    ${VOXOV_SOKOL_DIR}
    ${VOXOV_SOKOL_UTIL_DIR}
)
target_link_libraries(voxov PRIVATE game_runtime)
if(APPLE)
    target_link_libraries(voxov PRIVATE
        "-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework MetalKit"
        "-framework AudioToolbox"
    )
elseif(UNIX)
    target_link_libraries(voxov PRIVATE
        OpenGL::GL X11 Xi Xcursor m dl asound
    )
endif()

# ── Server ─────────────────────────────────────────────────────────────
add_executable(voxov_server
    src/game/server_main.cpp
)
target_include_directories(voxov_server PRIVATE src)
target_link_libraries(voxov_server PRIVATE engine_net)

# ── Asset cooker ───────────────────────────────────────────────────────
add_executable(voxov_asset_cooker
    src/tools/asset_cooker.cpp
    src/engine_assets/cooked_asset.cpp
)
target_include_directories(voxov_asset_cooker PRIVATE src)
target_link_libraries(voxov_asset_cooker PRIVATE fmt::fmt)

# ── MinGW runtime DLL bundling ────────────────────────────────────────
if(MINGW)
    set(VOXOV_MINGW_RUNTIME_DLLS "")
    set(VOXOV_MINGW_RUNTIME_DLL_NAMES
        libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll
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
                ${VOXOV_MINGW_RUNTIME_DLLS} $<TARGET_FILE_DIR:voxov>
        )
    endif()
endif()

# ── Install ────────────────────────────────────────────────────────────
install(TARGETS voxov voxov_server
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
if(MINGW AND VOXOV_MINGW_RUNTIME_DLLS)
    install(FILES ${VOXOV_MINGW_RUNTIME_DLLS} DESTINATION bin)
endif()
install(DIRECTORY assets/models/player/
    DESTINATION bin/assets/models/player
    FILES_MATCHING PATTERN "*.glb" PATTERN "*.md" PATTERN "LICENSE*"
)
install(DIRECTORY assets/fonts/
    DESTINATION bin/assets/fonts
    FILES_MATCHING PATTERN "*.ttf" PATTERN "*.otf" PATTERN "*.md" PATTERN "LICENSE*"
)

# ── Tests ──────────────────────────────────────────────────────────────
if(VOXOV_BUILD_TESTS)
    enable_testing()
    add_executable(voxov_tests
        src/tests/test_main.cpp
        src/platform/android_platform.cpp
        src/platform/web_platform.cpp
    )
    target_include_directories(voxov_tests PRIVATE src)
    target_link_libraries(voxov_tests PRIVATE engine engine_events engine_math engine_world engine_net engine_net_proto)
    add_test(NAME voxov_tests COMMAND voxov_tests)

    add_executable(voxov_event_bus_tests
        src/tests/event_bus_tests.cpp
    )
    target_include_directories(voxov_event_bus_tests PRIVATE src)
    target_link_libraries(voxov_event_bus_tests PRIVATE engine_events)
    add_test(NAME voxov_event_bus_tests COMMAND voxov_event_bus_tests)

    add_executable(voxov_net_stress
        src/tests/net_stress.cpp
    )
    target_include_directories(voxov_net_stress PRIVATE src)
    target_link_libraries(voxov_net_stress PRIVATE engine_net)
    add_test(NAME voxov_net_stress COMMAND voxov_net_stress)

    # ── Sokol smoke test ────────────────────────────────────────────────
    add_executable(voxov_sokol_smoke
        src/tests/sokol_smoke.cpp
    )
    target_include_directories(voxov_sokol_smoke PRIVATE
        src
        ${VOXOV_SOKOL_DIR}
        ${VOXOV_SOKOL_UTIL_DIR}
    )
    if(APPLE)
        target_link_libraries(voxov_sokol_smoke PRIVATE
            "-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework MetalKit"
            "-framework AudioToolbox"
        )
    elseif(WIN32)
        target_link_libraries(voxov_sokol_smoke PRIVATE d3d11 dxgi)
    elseif(UNIX)
        target_link_libraries(voxov_sokol_smoke PRIVATE
            OpenGL::GL X11 Xi Xcursor m dl asound
        )
    endif()
endif()
