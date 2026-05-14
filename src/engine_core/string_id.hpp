#pragma once

#include <cstdint>
#include <cstddef>

using StringId = uint64_t;

constexpr StringId string_id_fnv1a(const char* str, size_t len) {
    StringId hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<StringId>(static_cast<uint8_t>(str[i]));
        hash *= 1099511628211ULL;
    }
    return hash;
}

constexpr StringId operator"" _sid(const char* str, size_t len) {
    return string_id_fnv1a(str, len);
}
