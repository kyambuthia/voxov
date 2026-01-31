#pragma once

#include <memory>

#include <glm/glm.hpp>

#include <SDL3/SDL.h>

#include "renderer/vulkan_app.hpp"
#include "world/planet_world.hpp"

class Game {
public:
    Game();
    ~Game();

    void init(SDL_Window *window);
    SDL_AppResult handle_event(const SDL_Event &event);
    void tick();

private:
    SDL_Window *window = nullptr;
    std::unique_ptr<VulkanApp> vulkan_app = nullptr;
    PlanetWorld world;
};
