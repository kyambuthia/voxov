#define SDL_MAIN_USE_CALLBACKS

#include <iostream>
#include <memory>
#include "game.hpp"
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

struct AppState {
    SDL_Window *window = nullptr;
    std::unique_ptr<Game> game = nullptr;
};

SDL_AppResult SDL_AppInit(void **appstate, int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto state = std::make_unique<AppState>();
    state->window = SDL_CreateWindow(
        "VOXOV - Vulkan Test",
        800, 600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!state->window) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    try {
        state->game = std::make_unique<Game>();
        state->game->init(state->window);
    } catch (const std::exception& e) {
        SDL_Log("Vulkan initialization failed: %s", e.what());
        return SDL_APP_FAILURE;
    }

    std::cout << "SDL3 + Vulkan initialized successfully" << std::endl;

    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    auto state = static_cast<AppState *>(appstate);

    if (state && state->game) {
        return state->game->handle_event(*event);
    }

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    auto state = static_cast<AppState *>(appstate);

    if (state && state->game) {
        state->game->tick();
    }
    
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    auto state = static_cast<AppState *>(appstate);
    if (state) {
        state->game.reset();
        SDL_DestroyWindow(state->window);
        delete state;
    }

    (void)result;
    SDL_Quit();
    std::cout << "SDL3 + Vulkan quit" << std::endl;
}

