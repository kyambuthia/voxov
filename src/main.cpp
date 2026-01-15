#define SDL_MAIN_USE_CALLBACKS

#include <iostream>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        SDL_Log("init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    window = SDL_CreateWindow(
        "voxov",
	800, 600,
	SDL_WINDOW_RESIZABLE
    );

    if (!window) return SDL_APP_FAILURE;

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) return SDL_APP_FAILURE;

    std::cout << "SDL3 initialized successfully" << std::endl;
    *appstate = nullptr;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 125);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    SDL_Quit();
    std::cout << "SDL3 quit" << std::endl;
}

