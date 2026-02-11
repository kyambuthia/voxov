## VOXOV
voxov is a voxel game written with C++ using the vulkan API and SDL3.
voxov is a learning project as i learn computer graphics.

VOXOV is transitioning into a small, modern engine with Vulkan rendering, Jolt Physics,
and ENet-based multiplayer.

## Requirements
requirements to compile and build the vocov source.

CMAKE
Vulkan
JoltPhysics (submodule)

## Docs

Setup and build instructions:
- `docs/SETUP.md`

## Building the project
get all the dependencies at once with the --recurse-submodules option when cloning the project
`git clone --recurse-submodules github.com/kyambuthia/voxov.git && cd ./voxoc`

or just clone the project and then get the dependencies.
`git clone github.com/kyambuthia/voxov.git && cd ./voxov && git submodule init && git submodule update` 

once you have the dependencies, create a build directory, move into it and build the project.
`mkdir ./build && cd ./build && cmake ../ && cmake --build . `
