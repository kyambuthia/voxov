#include "engine_core/memory.hpp"

#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#endif

LinearArena::~LinearArena() {
    shutdown();
}

void LinearArena::init(size_t bytes) {
    shutdown();
    capacity = bytes;
    if (capacity == 0) {
        return;
    }

#ifdef _WIN32
    base = static_cast<uint8_t *>(_aligned_malloc(capacity, 16));
#else
    void *ptr = nullptr;
    if (posix_memalign(&ptr, 16, capacity) == 0) {
        base = static_cast<uint8_t *>(ptr);
    } else {
        base = nullptr;
    }
#endif
    offset = 0;
}

void LinearArena::reset() {
    offset = 0;
}

void LinearArena::shutdown() {
    if (base) {
#ifdef _WIN32
        _aligned_free(base);
#else
        std::free(base);
#endif
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
