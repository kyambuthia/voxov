# VOXOV Development Guide

This guide provides development practices, build commands, and coding standards for the VOXOV project - a voxel game built with C++, Vulkan, and SDL3.

## Build Commands

```bash
# Clone with dependencies
git clone --recurse-submodules github.com/kyambuthia/voxov.git && cd ./voxov
git submodule init && git submodule update

# Build project
mkdir ./build && cd ./build
cmake .. && cmake --build .

# Configure build options
cmake -DVOXOV_BUILD_TESTS=ON -DVOXOV_BUILD_EXAMPLES=ON -DVOXOV_ENABLE_ASSERTIONS=ON ..

# Build configurations
cmake --build . --config Release
cmake --build . --config Debug

# Manual shader compilation (auto-compiled during build)
glslc src/renderer/shaders/cube.vert -o cube.vert.spv
glslc src/renderer/shaders/cube.frag -o cube.frag.spv

# Build and run tests (when tests are added)
cmake -DVOXOV_BUILD_TESTS=ON .. && cmake --build . && ctest --output-on-failure

# Linting and formatting
find . -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" | xargs clang-format -i
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
run-clang-tidy -p .
clang-tidy -p . src/main.cpp
```

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

## Vulkan and SDL3 Guidelines

- Always check VkResult return values
- Use RAII wrappers for Vulkan objects when possible
- Follow Vulkan naming conventions (CamelCase for structs, UPPER_CASE for enums)
- Use descriptor sets and command buffers efficiently
- Use SDL3 callback-based application model (SDL_AppInit, SDL_AppIterate, SDL_AppQuit)
- Handle SDL_AppEvent for input and window events
- Leverage SDL's cross-platform abstractions for file I/O and threading
- Use SDL_Log for consistent logging across platforms
