#define SOKOL_NO_ENTRY
#define SOKOL_AUDIO_IMPL
#include "sokol_gfx.h"
#if defined(VOXOV_PLATFORM_WEB) || !defined(VOXOV_USE_SDL3)
#define SOKOL_APP_IMPL
#define SOKOL_GLUE_IMPL
#include "sokol_app.h"
#include "sokol_glue.h"
#endif
#include "sokol_audio.h"
#include "sokol_log.h"
