#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

#include "vulkan/vulkan.h"

static SDL_Window* window = NULL;

int main(int argc, char* argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("ERR: Video init failed. %s", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("voxov", 640, 480, SDL_WINDOW_VULKAN);
    if (!window)
    {
        SDL_Log("ERR: Window creation failed. %s", SDL_GetError());
        return 1;
    }

    bool running = true;
    SDL_Event e;
    while (running)
    {
        while(SDL_PollEvent(&e))
        { 
            if (e.type == SDL_EVENT_QUIT) 
            {
                running = false;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

