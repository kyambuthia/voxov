cmake_minimum_required(VERSION 3.10.0)

project(voxov LANGUAGES C CXX)

message("PROJECT NAME --> "${PROJECT_NAME})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/SDL" EXCLUDE_FROM_ALL)

find_package(Vulkan REQUIRED)

add_executable(${PROJECT_NAME} "${CMAKE_SOURCE_DIR}/src/main.cpp")

target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan SDL3::SDL3)


