#pragma once

#include "engine_core/string_id.hpp"

#include <cstdint>

enum class CvarFlags : uint8_t {
    None    = 0,
    Archive = 1 << 0,
    Cheat   = 1 << 1,
    ReadOnly = 1 << 2,
};

inline CvarFlags operator|(CvarFlags a, CvarFlags b) {
    return static_cast<CvarFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool operator&(CvarFlags a, CvarFlags b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

struct Cvar {
    StringId    name;
    CvarFlags   flags;
    float       value;
    float       default_value;
    const char* description;
    Cvar*       next;
};

void cvar_register(Cvar* cvar);
Cvar* cvar_find(StringId name);

void cvar_set_float(StringId name, float value);
float cvar_get_float(StringId name);
bool cvar_get_bool(StringId name);
int cvar_get_int(StringId name);

void cvar_parse_command_line(int argc, char** argv);

#define CVAR_FLOAT(var_name, default_val, flg, desc) \
    static Cvar cvar_##var_name = { \
        #var_name##_sid, flg, default_val, default_val, desc, nullptr \
    }; \
    static struct CvarRegistrar_##var_name { \
        CvarRegistrar_##var_name() { cvar_register(&cvar_##var_name); } \
    } cvar_registrar_##var_name##_instance

#define CVAR_BOOL(var_name, default_val, flg, desc) \
    CVAR_FLOAT(var_name, (default_val) ? 1.0f : 0.0f, flg, desc)
