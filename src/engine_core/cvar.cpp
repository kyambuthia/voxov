#include "engine_core/cvar.hpp"

#include <cstdlib>
#include <cstring>

static Cvar* g_cvar_list = nullptr;

void cvar_register(Cvar* cvar) {
    cvar->next = g_cvar_list;
    g_cvar_list = cvar;
}

Cvar* cvar_find(StringId name) {
    for (Cvar* c = g_cvar_list; c; c = c->next) {
        if (c->name == name) return c;
    }
    return nullptr;
}

void cvar_set_float(StringId name, float value) {
    Cvar* c = cvar_find(name);
    if (c && !(c->flags & CvarFlags::ReadOnly)) {
        c->value = value;
    }
}

float cvar_get_float(StringId name) {
    Cvar* c = cvar_find(name);
    return c ? c->value : 0.0f;
}

bool cvar_get_bool(StringId name) {
    return cvar_get_float(name) != 0.0f;
}

int cvar_get_int(StringId name) {
    return static_cast<int>(cvar_get_float(name));
}

void cvar_parse_command_line(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg[0] != '-' || arg[1] != '-') continue;

        const char* name = arg + 2;
        StringId id = string_id_fnv1a(name, std::strlen(name));

        Cvar* c = cvar_find(id);
        if (!c) continue;

        if (i + 1 < argc && argv[i + 1][0] != '-') {
            float val = static_cast<float>(std::strtod(argv[++i], nullptr));
            cvar_set_float(id, val);
        } else {
            cvar_set_float(id, 1.0f);
        }
    }
}
