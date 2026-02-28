#pragma once

#include "engine_input/input_state.hpp"

#include <array>
#include <cstdint>
#include <string>

#include <glm/vec2.hpp>

enum class MiniGameType : uint8_t {
    Snake = 0,
    Golf = 1,
    Tetris = 2,
    Racing = 3,
    TicTacToe = 4
};

struct SnakeState {
    static constexpr int k_grid_w = 10;
    static constexpr int k_grid_h = 10;
    std::array<glm::ivec2, 128> body{};
    int length = 0;
    glm::ivec2 direction = glm::ivec2(1, 0);
    glm::ivec2 pending_direction = glm::ivec2(1, 0);
    glm::ivec2 food = glm::ivec2(6, 5);
    float step_timer = 0.0f;
};

struct GolfState {
    glm::vec2 ball = glm::vec2(0.0f);
    glm::vec2 velocity = glm::vec2(0.0f);
    glm::vec2 hole = glm::vec2(7.0f, 5.0f);
    float aim_radians = 0.0f;
    float power = 0.6f;
    int strokes = 0;
};

struct TetrisState {
    static constexpr int k_board_w = 10;
    static constexpr int k_board_h = 16;
    std::array<uint8_t, k_board_w * k_board_h> board{};
    int active_shape = 0;
    int active_rotation = 0;
    int active_x = 4;
    int active_y = 1;
    float fall_timer = 0.0f;
};

struct RacingState {
    float track_progress = 0.0f;
    float speed = 0.0f;
    int lap = 0;
    int next_checkpoint = 0;
    float elapsed = 0.0f;
};

struct TicTacToeState {
    std::array<uint8_t, 9> board{};
    int cursor = 4;
    uint8_t winner = 0;
    int turns = 0;
};

struct MiniGameState {
    MiniGameType type = MiniGameType::Snake;
    bool active = false;
    bool completed = false;
    bool victory = false;
    int score = 0;
    uint32_t seed = 1;
    float input_cooldown = 0.0f;

    SnakeState snake{};
    GolfState golf{};
    TetrisState tetris{};
    RacingState racing{};
    TicTacToeState tictactoe{};
};

const char *minigame_name(MiniGameType type);
void minigame_begin(MiniGameState &state, MiniGameType type, uint32_t seed);
void minigame_tick(MiniGameState &state, const InputState &input, float dt);
std::string minigame_status_text(const MiniGameState &state);

