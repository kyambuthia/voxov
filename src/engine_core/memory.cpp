#include "engine_core/memory.hpp"

#include <cstdlib>

LinearArena::~LinearArena() {
    shutdown();
}

void LinearArena::init(size_t bytes) {
    shutdown();
    capacity = bytes;
    base = static_cast<uint8_t *>(std::aligned_alloc(16, capacity));
    offset = 0;
}

void LinearArena::reset() {
    offset = 0;
}

void LinearArena::shutdown() {
    if (base) {
        std::free(base);
        base = nullptr;
    }
    capacity = 0;
    offset = 0;
}

void *LinearArena::alloc(size_t bytes, size_t alignment) {
    size_t current = reinterpret_cast<size_t>(base) + offset;
    size_t aligned = (current + (alignment - 1)) & ~(alignment - 1);
    size_t new_offset = aligned - reinterpret_cast<size_t>(base) + bytes;
    if (new_offset > capacity) {
        return nullptr;
    }
    offset = new_offset;
    return reinterpret_cast<void *>(aligned);
}
