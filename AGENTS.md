# VOXOV Development Guide

This guide provides development practices, build commands, and coding standards for the VOXOV project - a voxel game built with C++, Vulkan, and SDL3.

## Build Commands

### Basic Build
```bash
# Clone with dependencies
git clone --recurse-submodules github.com/kyambuthia/voxov.git && cd ./voxov

# Or initialize submodules manually
git submodule init && git submodule update

# Build the project
mkdir ./build && cd ./build
cmake ..
cmake --build .
```

### Build Options
```bash
# Configure build options
cmake -DVOXOV_BUILD_TESTS=ON \
      -DVOXOV_BUILD_EXAMPLES=ON \
      -DVOXOV_ENABLE_ASSERTIONS=ON \
      ..

# Build specific configuration
cmake --build . --config Release
cmake --build . --config Debug
```

### Testing
```bash
# Build and run tests
cmake -DVOXOV_BUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure

# Run single test (when test framework is added)
ctest -R <test_name> --output-on-failure
```

### Linting and Formatting
```bash
# Format code with clang-format (using SDL's configuration)
find . -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" | xargs clang-format -i

# Static analysis with clang-tidy
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
run-clang-tidy -p .

# Alternative: Check only specific files
clang-tidy -p . src/main.cpp
```

## Code Style Guidelines

### Formatting Standards
Based on SDL's `.clang-format` configuration:

- **Indentation**: 4 spaces (no tabs)
- **Line Length**: No strict limit (ColumnLimit: 0)
- **Pointer Alignment**: Right (`type * ptr` not `type* ptr`)
- **Brace Style**: Custom - opening brace on new line for functions/classes/namespaces
- **Case Labels**: Aligned with switch statement (no indentation)
- **Include Blocks**: Preserve existing order (don't auto-sort)

### Naming Conventions
```cpp
// Files: kebab-case for directories, camelCase/PascalCase for files
src/main.cpp
src/renderer/vulkanRenderer.cpp

// Variables: snake_case
static SDL_Window *window = nullptr;
int window_width = 800;

// Functions: snake_case for C, PascalCase for C++
SDL_AppResult SDL_AppInit(void **appstate, int argc, char* argv[]);
class VulkanRenderer { void InitializeDevice(); };

// Constants: UPPER_SNAKE_CASE
const int MAX_FRAMES_IN_FLIGHT = 3;

// Classes/Structs: PascalCase
class VulkanContext;
struct VertexBuffer;

// Private members: trailing underscore or m_ prefix
class Renderer {
private:
    VkDevice device_;
    int max_frames_;
};
```

### Import and Include Style
```cpp
// System headers first
#include <iostream>
#include <vector>

// External dependencies
#include <vulkan/vulkan.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

// Project headers
#include "renderer/vulkan_renderer.h"
#include "utils/logger.h"
```

### Type Guidelines
```cpp
// Use standard C++ types
#include <cstdint>
int32_t texture_width;
uint64_t memory_size;

// Prefer smart pointers
#include <memory>
std::unique_ptr<VulkanRenderer> renderer;
std::shared_ptr<Buffer> vertex_buffer;

// Use SDL and Vulkan native types where appropriate
SDL_Window *window;
VkDevice device;
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

// Use assertions for internal invariants
#include <cassert>
assert(device != VK_NULL_HANDLE && "Vulkan device must be valid");

// Resource cleanup with RAII
class VulkanBuffer {
public:
    ~VulkanBuffer() {
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer_, nullptr);
        }
    }
};
```

### Memory Management
```cpp
// Prefer RAII and smart pointers
class TextureManager {
    std::unique_ptr<uint8_t[]> texture_data_;
    std::vector<VkImage> images_;
};

// Manual resource management for Vulkan objects
void Cleanup() {
    if (shader_module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, shader_module_, nullptr);
        shader_module_ = VK_NULL_HANDLE;
    }
}
```

### Code Organization
```cpp
// File structure: declarations first, definitions second
class VulkanRenderer {
public:
    bool Initialize();
    void Shutdown();

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    
    bool CreateInstance();
    bool CreateDevice();
};

bool VulkanRenderer::Initialize() {
    if (!CreateInstance()) return false;
    if (!CreateDevice()) return false;
    return true;
}
```

### Comments and Documentation
```cpp
// Use clear, concise comments
// Initialize SDL3 with video and audio subsystems
if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {

// Document complex algorithms or non-obvious choices
/* 
 * We use a double-buffered swap chain to avoid tearing while
 * maintaining compatibility with older hardware that doesn't support
 * mailbox present modes.
 */
```

### Platform-Specific Code
```cpp
// Use preprocessor guards for platform-specific code
#ifdef _WIN32
    // Windows-specific code
    #include <windows.h>
#elif defined(__linux__)
    // Linux-specific code
    #include <pthread.h>
#endif

// Prefer cross-platform SDL abstractions when available
SDL_Delay(16);  // instead of Sleep() or usleep()
```

## Development Workflow

1. **Before committing**: Run clang-format and clang-tidy
2. **Testing**: Enable VOXOV_BUILD_TESTS when adding new features
3. **Performance**: Profile with Release builds, use Debug for development
4. **Documentation**: Update README.md for significant API changes
5. **Dependencies**: Update submodules when SDL3 or other dependencies change

## Vulkan-Specific Guidelines

- Always check VkResult return values
- Use RAII wrappers for Vulkan objects when possible
- Validate with VK_LAYER_KHRONOS_validation in Debug builds
- Follow Vulkan naming conventions (CamelCase for structs, UPPER_CASE for enums)
- Use descriptor sets and command buffers efficiently

## SDL3 Integration

- Use SDL3 callback-based application model (SDL_AppInit, SDL_AppIterate, SDL_AppQuit)
- Handle SDL_AppEvent for input and window events
- Leverage SDL's cross-platform abstractions for file I/O and threading
- Use SDL_Log for consistent logging across platforms