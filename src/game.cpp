#include "game.hpp"

#include <stdexcept>

Game::Game() {}

Game::~Game() {}

void Game::init(SDL_Window *sdl_window) {
    window = sdl_window;

    vulkan_app = std::make_unique<VulkanApp>();
    vulkan_app->init(window);

    world = PlanetWorld(glm::vec3(0.0f), 64.0f, 2.0f);
}

SDL_AppResult Game::handle_event(const SDL_Event &event) {
    if (event.type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (event.type == SDL_EVENT_WINDOW_RESIZED && vulkan_app) {
        vulkan_app->handle_resize(event.window.data1, event.window.data2);
    }

    return SDL_APP_CONTINUE;
}

void Game::tick() {
    if (vulkan_app) {
        vulkan_app->draw_frame();
    }

    SDL_Delay(16);
}
