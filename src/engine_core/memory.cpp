#include "engine_core/memory.hpp"

#include <atomic>
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
std::atomic<uint64_t> g_current_allocations{0};
std::atomic<uint64_t> g_total_allocations{0};
std::atomic<uint64_t> g_current_bytes{0};
std::atomic<uint64_t> g_total_bytes{0};

void release_active(uint64_t allocations, uint64_t bytes) {
    if (allocations > 0) {
        g_current_allocations.fetch_sub(allocations, std::memory_order_relaxed);
    }
    if (bytes > 0) {
        g_current_bytes.fetch_sub(bytes, std::memory_order_relaxed);
    }
}
} // namespace

MemoryStats engine_memory_stats() {
    return MemoryStats{
        .current_allocations =
            g_current_allocations.load(std::memory_order_relaxed),
        .total_allocations = g_total_allocations.load(std::memory_order_relaxed),
        .current_bytes = g_current_bytes.load(std::memory_order_relaxed),
        .total_bytes = g_total_bytes.load(std::memory_order_relaxed),
    };
}

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
    release_active(active_allocations, active_bytes);
    active_allocations = 0;
    active_bytes = 0;
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
    release_active(active_allocations, active_bytes);
    active_allocations = 0;
    active_bytes = 0;
}

void *LinearArena::alloc(size_t bytes, size_t alignment) {
    size_t current = reinterpret_cast<size_t>(base) + offset;
    size_t aligned = (current + (alignment - 1)) & ~(alignment - 1);
    size_t new_offset = aligned - reinterpret_cast<size_t>(base) + bytes;
    if (new_offset > capacity) {
        return nullptr;
    }
    offset = new_offset;
    active_allocations += 1;
    active_bytes += bytes;
    g_current_allocations.fetch_add(1, std::memory_order_relaxed);
    g_total_allocations.fetch_add(1, std::memory_order_relaxed);
    g_current_bytes.fetch_add(bytes, std::memory_order_relaxed);
    g_total_bytes.fetch_add(bytes, std::memory_order_relaxed);
    return reinterpret_cast<void *>(aligned);
}
