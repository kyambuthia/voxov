# VOXOV Development Guide

This guide provides development practices, build commands, and coding standards for VOXOV.
VOXOV is transitioning from a learning project into a serious rendering/game engine focused on
building beautiful, playable games (including classic-style visuals) with multiplayer and co-op support.

## Engine Direction (High Level)

- **Rendering**: Vulkan-first renderer with a clean abstraction layer for materials, meshes, and frame graph.
- **Game**: Deterministic simulation loop, clean input pipeline, and data-driven content.
- **Multiplayer**: Authoritative server with client-side prediction and snapshot interpolation.
- **Quality**: Stable builds, clear module boundaries, and strong tooling (profiling, validation, capture).

## Build Commands

```bash
# Clone with dependencies
git clone --recurse-submodules github.com/kyambuthia/voxov.git && cd ./voxov
git submodule init && git submodule update

# Build output policy (required)
# Always build under ./build/<target>/...
# - desktop: ./build/desktop/main
# - web: ./build/web/main
# - android native cmake: ./build/android/arm64-cmake

# Build project (desktop)
cmake -S . -B ./build/desktop/main
cmake --build ./build/desktop/main

# Configure build options
cmake -S . -B ./build/desktop/main -DVOXOV_BUILD_TESTS=ON -DVOXOV_BUILD_EXAMPLES=ON -DVOXOV_ENABLE_ASSERTIONS=ON

# Build configurations
cmake --build ./build/desktop/main --config Release
cmake --build ./build/desktop/main --config Debug

# Web (Emscripten)
EM_CACHE=./build/web/cache emcmake cmake -S . -B ./build/web/main -G Ninja
EM_CACHE=./build/web/cache cmake --build ./build/web/main --parallel

# Android native CMake (if building directly without Gradle)
cmake -S . -B ./build/android/arm64-cmake \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=29
cmake --build ./build/android/arm64-cmake --parallel

# Manual shader compilation (auto-compiled during build)
glslc src/renderer/shaders/cube.vert -o cube.vert.spv
glslc src/renderer/shaders/cube.frag -o cube.frag.spv

# Build and run tests (when tests are added)
cmake -S . -B ./build/desktop/tests -DVOXOV_BUILD_TESTS=ON && cmake --build ./build/desktop/tests && ctest --test-dir ./build/desktop/tests --output-on-failure

# Linting and formatting
find . -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" | xargs clang-format -i
cmake -S . -B ./build/desktop/clang -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
run-clang-tidy -p ./build/desktop/clang
clang-tidy -p ./build/desktop/clang src/main.cpp
```

## Build Directory Rule

Agents must not create top-level build directories like `build-*` in repo root.
All generated build outputs must live under `./build/<target>/...`.

## Code Style Guidelines

Based on SDL's `.clang-format` configuration:
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: No strict limit (ColumnLimit: 0)
- **Pointer Alignment**: Right (`type * ptr` not `type* ptr`)
- **Brace Style**: Custom - opening brace on new line for functions/classes/namespaces
- **Case Labels**: Aligned with switch statement (no indentation)
- **Include Blocks**: Preserve existing order (don't auto-sort)

### Naming Conventions
```cpp
// Files: snake_case
src/main.cpp, src/renderer/vulkan_app.cpp
// Variables: snake_case
static SDL_Window *window = nullptr;
// Functions: snake_case for C, PascalCase for C++
SDL_AppResult SDL_AppInit(void **appstate, int argc, char* argv[]);
class VulkanRenderer { void InitializeDevice(); };
// Constants: UPPER_SNAKE_CASE
const int MAX_FRAMES_IN_FLIGHT = 3;
// Classes/Structs: PascalCase
class VulkanApp; struct Vertex; struct UniformBufferObject;
// Private members: snake_case (no trailing underscore)
class VulkanApp {
private:
    VkInstance instance;
    VkDevice device;
};
```

### Import, Types, and Memory Management
```cpp
// System headers first
#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
// External dependencies
#include <vulkan/vulkan.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
// Project headers
#include "renderer/vulkan_app.hpp"

// Use standard C++ types and smart pointers
int32_t texture_width;
uint64_t memory_size;
std::unique_ptr<VulkanApp> vulkan_app;

// Prefer RAII and smart pointers
static std::unique_ptr<VulkanApp> vulkan_app = nullptr;

// Manual resource management for Vulkan objects
void cleanup() {
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}
```

### Error Handling
```cpp
// Return error codes, follow SDL pattern
SDL_AppResult InitializeVulkan() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

// Use exceptions for Vulkan errors
try {
    vulkan_app->init(window);
} catch (const std::exception& e) {
    SDL_Log("Vulkan initialization failed: %s", e.what());
    return SDL_APP_FAILURE;
}

// Resource cleanup with RAII
VulkanApp::~VulkanApp() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
    }
}
```

### Code Organization
```cpp
// File structure: declarations first, definitions second
class VulkanApp {
public:
    void init(SDL_Window* window);
    void cleanup();
private:
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    void create_instance();
    void create_logical_device();
};
```

### Platform-Specific Code
```cpp
#ifdef _WIN32
    target_link_libraries(${PROJECT_NAME} PRIVATE setupapi winmm)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread dl)
endif()
```

## Development Workflow

1. **Before committing**: Run clang-format and clang-tidy
2. **Testing**: Currently no tests - add test framework before enabling VOXOV_BUILD_TESTS
3. **Performance**: Profile with Release builds, use Debug for development
4. **Dependencies**: Update submodules when SDL3 or other dependencies change

## Release Artifact Runtime Tenet (Strict)

Agents must never ship release artifacts that are not runnable on a clean target machine.

1. **Bundle, don't bare-binary**: Do not publish only `voxov` or `voxov.exe`. Publish a runnable bundle (directory/tar/zip) containing executable + required runtime files.
2. **Windows runtime completeness**: Windows artifacts must include required runtime DLLs (at minimum toolchain/runtime DLLs and any engine-loaded files like shaders).
3. **Linux runtime completeness**: Linux artifacts must include non-system runtime `.so` dependencies and engine-loaded runtime files (for example shader `.spv` files) or provide an explicit launcher with correct library path setup.
4. **No release without runtime validation**: Before tagging/publishing, validate startup from the packaged artifact form (not from build tree). If startup validation fails, release is blocked.
5. **CI policy**: Release workflows must package runnable artifacts and must not mark a release successful if platform artifacts are known non-runnable.
6. **Scope discipline**: If asked to fix one platform (for example Windows packaging), do not alter unrelated platform release flows unless explicitly requested.

## Vulkan and SDL3 Guidelines

- Always check VkResult return values
- Use RAII wrappers for Vulkan objects when possible
- Follow Vulkan naming conventions (CamelCase for structs, UPPER_CASE for enums)
- Use descriptor sets and command buffers efficiently
- Use SDL3 callback-based application model (SDL_AppInit, SDL_AppIterate, SDL_AppQuit)
- Handle SDL_AppEvent for input and window events
- Leverage SDL's cross-platform abstractions for file I/O and threading
- Use SDL_Log for consistent logging across platforms

## Web Target Guidance

VOXOV targets web in addition to desktop/mobile/console classes. Follow these rules:

1. Prefer a lean WebGL2-compatible rendering path for web bring-up; keep Vulkan as primary desktop path.
2. Keep gameplay/simulation logic renderer-agnostic and platform-agnostic.
3. Reuse mobile constraints for web by default:
   - strict memory budgets
   - bounded chunk residency
   - conservative physics budgets
4. Avoid blocking IO patterns and platform assumptions that do not map to browser sandboxes.
5. Keep input abstraction unified across desktop/mouse, mobile/touch, and web pointer/touch.
6. Treat web as a first-class CI target once baseline rendering/input loop is stable.

## XR Target Guidance

VOXOV also targets 3D headsets as an explicit platform class:

1. Meta Quest/Oculus and PC VR headsets should use an OpenXR-based runtime path.
2. Apple Vision Pro should be treated as a dedicated visionOS target with platform-specific integration.
3. Engine/gameplay systems must stay XR-agnostic; XR specifics belong in platform and renderer layers.
4. XR rendering requirements are mandatory:
   - stereo view/projection per eye
   - strict frame pacing and low-latency pose updates
   - comfort-safe locomotion options
5. XR support should follow after mobile/web baseline stabilization, not block core shipping milestones.
