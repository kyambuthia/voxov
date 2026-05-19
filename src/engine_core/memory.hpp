#pragma once

#include <cstddef>
#include <cstdint>

struct MemoryStats {
    uint64_t current_allocations = 0;
    uint64_t total_allocations = 0;
    uint64_t current_bytes = 0;
    uint64_t total_bytes = 0;
};

MemoryStats engine_memory_stats();

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
    uint64_t active_allocations = 0;
    uint64_t active_bytes = 0;
};
