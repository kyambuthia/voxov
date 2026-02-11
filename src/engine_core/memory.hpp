#pragma once

#include <cstddef>
#include <cstdint>

class LinearArena {
public:
    LinearArena() = default;
    ~LinearArena();

    void init(size_t bytes);
    void reset();
    void shutdown();
    void *alloc(size_t bytes, size_t alignment = 16);

private:
    uint8_t *base = nullptr;
    size_t capacity = 0;
    size_t offset = 0;
};
