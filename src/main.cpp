#include <iostream>

#include "SDL3/SDL.h"
#include "vulkan/vulkan.h"

static SDL_Window *window=NULL;

SDL_AppResult SDL_APPInit(void **appstate, int argc, char *argv)
{
        SDL_SetAppMetadata("Example renderer", "0.6", "com.voxov.rndr");
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
                SDL_Log("Couldn't init SDL, %s", SDL_GetError());
                return SDL_APP_FAILURE;
        }

        if (!SDL_CreateWindow("rndr", 640, 480, &window)){
        {
                SDL_Log("Couldn't create a window, %s", SDL_GetError());
                return SDL_APP_FAILURE;
        }

        return SDL_APP_CONTINUE;
}



