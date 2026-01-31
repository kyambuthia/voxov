cmake_minimum_required(VERSION 3.16.0)

project(voxov
    VERSION 1.0.0
    DESCRIPTION "Voxov - A Vulkan-based application"
    LANGUAGES C CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
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

# Dependencies
add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/SDL" EXCLUDE_FROM_ALL)

# Find packages
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)
find_package(Vulkan COMPONENTS glslc REQUIRED)

# Collect source files
file(GLOB_RECURSE VOXOV_SOURCES 
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)

# Create executable
add_executable(${PROJECT_NAME} ${VOXOV_SOURCES})

# Target properties
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/dependencies/SDL/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
)

# Platform-specific linking
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        setupapi
        winmm
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        pthread
        dl
    )
endif()

# Debug information
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:VOXOV_DEBUG>
    $<$<CONFIG:Release>:VOXOV_RELEASE>
    $<$<BOOL:${VOXOV_ENABLE_ASSERTIONS}>:VOXOV_ENABLE_ASSERTIONS>
)

# Shader compilation
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cube.vert.spv
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders/cube.vert -o ${CMAKE_CURRENT_BINARY_DIR}/cube.vert.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders/cube.vert
    VERBATIM
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cube.frag.spv
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders/cube.frag -o ${CMAKE_CURRENT_BINARY_DIR}/cube.frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders/cube.frag
    VERBATIM
)

# Add shader target
add_custom_target(shaders ALL
    DEPENDS 
    ${CMAKE_CURRENT_BINARY_DIR}/cube.vert.spv
    ${CMAKE_CURRENT_BINARY_DIR}/cube.frag.spv
)

# Make main executable depend on shaders
add_dependencies(${PROJECT_NAME} shaders)

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)


