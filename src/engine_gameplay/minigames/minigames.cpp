#include "engine_gameplay/minigames/minigames.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
uint32_t lcg_next(uint32_t &seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

int tic_tac_toe_winner(const std::array<uint8_t, 9> &board) {
    constexpr int k_lines[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
        {0, 4, 8}, {2, 4, 6}
    };
    for (const auto &line : k_lines) {
        const uint8_t a = board[line[0]];
        if (a != 0 && a == board[line[1]] && a == board[line[2]]) {
            return static_cast<int>(a);
        }
    }
    return 0;
}

int tetris_piece_count() {
    return 4;
}

glm::ivec2 tetris_cell(int shape, int rot, int i) {
    // Compact subset of tetrominoes for fast arcade rounds.
    static constexpr glm::ivec2 k_shape_rot[4][4][4] = {
        { // I
            {{-1, 0}, {0, 0}, {1, 0}, {2, 0}},
            {{0, -1}, {0, 0}, {0, 1}, {0, 2}},
            {{-1, 1}, {0, 1}, {1, 1}, {2, 1}},
            {{1, -1}, {1, 0}, {1, 1}, {1, 2}},
        },
        { // O
            {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
            {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
            {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
            {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
        },
        { // T
            {{-1, 0}, {0, 0}, {1, 0}, {0, 1}},
            {{0, -1}, {0, 0}, {0, 1}, {1, 0}},
            {{-1, 0}, {0, 0}, {1, 0}, {0, -1}},
            {{0, -1}, {0, 0}, {0, 1}, {-1, 0}},
        },
        { // L
            {{-1, 0}, {0, 0}, {1, 0}, {1, 1}},
            {{0, -1}, {0, 0}, {0, 1}, {1, -1}},
            {{-1, -1}, {-1, 0}, {0, 0}, {1, 0}},
            {{-1, 1}, {0, -1}, {0, 0}, {0, 1}},
        }
    };
    return k_shape_rot[shape % 4][rot % 4][i % 4];
}

bool tetris_valid(const TetrisState &t, int x, int y, int shape, int rot) {
    for (int i = 0; i < 4; ++i) {
        const glm::ivec2 c = tetris_cell(shape, rot, i);
        const int cx = x + c.x;
        const int cy = y + c.y;
        if (cx < 0 || cx >= TetrisState::k_board_w || cy < 0 || cy >= TetrisState::k_board_h) {
            return false;
        }
        if (t.board[static_cast<size_t>(cy * TetrisState::k_board_w + cx)] != 0) {
            return false;
        }
    }
    return true;
}

void tetris_lock_piece(TetrisState &t) {
    for (int i = 0; i < 4; ++i) {
        const glm::ivec2 c = tetris_cell(t.active_shape, t.active_rotation, i);
        const int x = t.active_x + c.x;
        const int y = t.active_y + c.y;
        if (x >= 0 && x < TetrisState::k_board_w && y >= 0 && y < TetrisState::k_board_h) {
            t.board[static_cast<size_t>(y * TetrisState::k_board_w + x)] = 1;
        }
    }
}

int tetris_clear_lines(TetrisState &t) {
    int cleared = 0;
    for (int y = TetrisState::k_board_h - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < TetrisState::k_board_w; ++x) {
            if (t.board[static_cast<size_t>(y * TetrisState::k_board_w + x)] == 0) {
                full = false;
                break;
            }
        }
        if (!full) {
            continue;
        }
        ++cleared;
        for (int row = y; row > 0; --row) {
            for (int x = 0; x < TetrisState::k_board_w; ++x) {
                t.board[static_cast<size_t>(row * TetrisState::k_board_w + x)] =
                    t.board[static_cast<size_t>((row - 1) * TetrisState::k_board_w + x)];
            }
        }
        for (int x = 0; x < TetrisState::k_board_w; ++x) {
            t.board[static_cast<size_t>(x)] = 0;
        }
        ++y;
    }
    return cleared;
}

void snake_respawn_food(MiniGameState &state) {
    for (int attempt = 0; attempt < 128; ++attempt) {
        const int x = static_cast<int>(lcg_next(state.seed) % SnakeState::k_grid_w);
        const int y = static_cast<int>(lcg_next(state.seed) % SnakeState::k_grid_h);
        bool occupied = false;
        for (int i = 0; i < state.snake.length; ++i) {
            if (state.snake.body[static_cast<size_t>(i)] == glm::ivec2(x, y)) {
                occupied = true;
                break;
            }
        }
        if (!occupied) {
            state.snake.food = glm::ivec2(x, y);
            return;
        }
    }
}

void begin_snake(MiniGameState &state) {
    state.snake = SnakeState{};
    state.snake.length = 3;
    state.snake.body[0] = glm::ivec2(4, 5);
    state.snake.body[1] = glm::ivec2(3, 5);
    state.snake.body[2] = glm::ivec2(2, 5);
    snake_respawn_food(state);
}

void begin_golf(MiniGameState &state) {
    state.golf = GolfState{};
    state.golf.ball = glm::vec2(0.0f, 0.0f);
    state.golf.hole = glm::vec2(7.5f, 5.0f);
}

void begin_tetris(MiniGameState &state) {
    state.tetris = TetrisState{};
    state.tetris.active_shape = static_cast<int>(lcg_next(state.seed) % static_cast<uint32_t>(tetris_piece_count()));
    state.tetris.active_rotation = 0;
    state.tetris.active_x = 4;
    state.tetris.active_y = 1;
}

void begin_racing(MiniGameState &state) {
    state.racing = RacingState{};
}

void begin_tic_tac_toe(MiniGameState &state) {
    state.tictactoe = TicTacToeState{};
}

void tick_snake(MiniGameState &state, const InputState &input, float dt) {
    if (std::fabs(input.move.x) > 0.6f) {
        state.snake.pending_direction = glm::ivec2((input.move.x > 0.0f) ? 1 : -1, 0);
    } else if (std::fabs(input.move.y) > 0.6f) {
        state.snake.pending_direction = glm::ivec2(0, (input.move.y > 0.0f) ? 1 : -1);
    }

    if ((state.snake.pending_direction.x + state.snake.direction.x) != 0 ||
        (state.snake.pending_direction.y + state.snake.direction.y) != 0) {
        state.snake.direction = state.snake.pending_direction;
    }

    state.snake.step_timer += dt;
    if (state.snake.step_timer < 0.18f) {
        return;
    }
    state.snake.step_timer = 0.0f;

    glm::ivec2 next = state.snake.body[0] + state.snake.direction;
    if (next.x < 0 || next.y < 0 || next.x >= SnakeState::k_grid_w || next.y >= SnakeState::k_grid_h) {
        state.completed = true;
        state.victory = false;
        return;
    }
    for (int i = 0; i < state.snake.length; ++i) {
        if (state.snake.body[static_cast<size_t>(i)] == next) {
            state.completed = true;
            state.victory = false;
            return;
        }
    }

    const bool ate_food = (next == state.snake.food);
    const int target_length = std::min<int>(static_cast<int>(state.snake.body.size()), state.snake.length + (ate_food ? 1 : 0));
    for (int i = target_length - 1; i > 0; --i) {
        state.snake.body[static_cast<size_t>(i)] = state.snake.body[static_cast<size_t>(i - 1)];
    }
    state.snake.body[0] = next;
    state.snake.length = target_length;

    if (ate_food) {
        state.score += 10;
        snake_respawn_food(state);
        if (state.snake.length >= 24) {
            state.completed = true;
            state.victory = true;
        }
    }
}

void tick_golf(MiniGameState &state, const InputState &input, float dt) {
    state.golf.aim_radians += input.move.x * dt * 1.8f;
    state.golf.power = std::clamp(state.golf.power + input.move.y * dt * 0.9f, 0.15f, 1.0f);

    const float speed = glm::length(state.golf.velocity);
    if (speed < 0.03f) {
        state.golf.velocity = glm::vec2(0.0f);
        if (input.interact_pressed) {
            const glm::vec2 dir(std::cos(state.golf.aim_radians), std::sin(state.golf.aim_radians));
            state.golf.velocity = dir * (state.golf.power * 11.0f);
            state.golf.strokes += 1;
        }
    }

    state.golf.ball += state.golf.velocity * dt;
    state.golf.velocity *= std::pow(0.28f, dt);

    auto bounce_axis = [](float &p, float &v, float min_v, float max_v) {
        if (p < min_v) {
            p = min_v;
            v = std::fabs(v) * 0.75f;
        } else if (p > max_v) {
            p = max_v;
            v = -std::fabs(v) * 0.75f;
        }
    };
    bounce_axis(state.golf.ball.x, state.golf.velocity.x, -8.0f, 8.0f);
    bounce_axis(state.golf.ball.y, state.golf.velocity.y, -6.0f, 6.0f);

    const float dist = glm::length(state.golf.ball - state.golf.hole);
    if (dist < 0.45f && glm::length(state.golf.velocity) < 0.55f) {
        state.completed = true;
        state.victory = true;
        state.score = std::max(0, 100 - state.golf.strokes * 7);
    }
}

void tick_tetris(MiniGameState &state, const InputState &input, float dt) {
    auto &t = state.tetris;

    if (state.input_cooldown <= 0.0f && std::fabs(input.move.x) > 0.6f) {
        const int dir = (input.move.x > 0.0f) ? 1 : -1;
        if (tetris_valid(t, t.active_x + dir, t.active_y, t.active_shape, t.active_rotation)) {
            t.active_x += dir;
        }
        state.input_cooldown = 0.12f;
    }

    if (state.input_cooldown <= 0.0f && input.jump_pressed) {
        const int next_rot = (t.active_rotation + 1) % 4;
        if (tetris_valid(t, t.active_x, t.active_y, t.active_shape, next_rot)) {
            t.active_rotation = next_rot;
        }
        state.input_cooldown = 0.12f;
    }

    const float fall_interval = (input.crouch_held || input.move.y < -0.4f) ? 0.05f : 0.35f;
    t.fall_timer += dt;
    if (t.fall_timer < fall_interval) {
        return;
    }
    t.fall_timer = 0.0f;

    if (tetris_valid(t, t.active_x, t.active_y + 1, t.active_shape, t.active_rotation)) {
        t.active_y += 1;
        return;
    }

    tetris_lock_piece(t);
    const int cleared = tetris_clear_lines(t);
    if (cleared > 0) {
        state.score += cleared * 100;
    }

    t.active_shape = static_cast<int>(lcg_next(state.seed) % static_cast<uint32_t>(tetris_piece_count()));
    t.active_rotation = 0;
    t.active_x = 4;
    t.active_y = 1;

    if (!tetris_valid(t, t.active_x, t.active_y, t.active_shape, t.active_rotation)) {
        state.completed = true;
        state.victory = state.score >= 400;
    }
    if (state.score >= 1200) {
        state.completed = true;
        state.victory = true;
    }
}

void tick_racing(MiniGameState &state, const InputState &input, float dt) {
    auto &r = state.racing;
    r.elapsed += dt;

    const float throttle = std::clamp(input.move.y, -1.0f, 1.0f);
    r.speed += throttle * 22.0f * dt;
    r.speed -= r.speed * std::min(1.0f, dt * 1.6f);
    r.speed = std::clamp(r.speed, 0.0f, 42.0f);

    const float steering_penalty = std::fabs(input.move.x) * 5.0f;
    const float effective_speed = std::max(0.0f, r.speed - steering_penalty);
    r.track_progress += effective_speed * dt;

    constexpr float k_checkpoint_stride = 65.0f;
    constexpr int k_checkpoints = 4;
    while (r.track_progress >= k_checkpoint_stride) {
        r.track_progress -= k_checkpoint_stride;
        r.next_checkpoint = (r.next_checkpoint + 1) % k_checkpoints;
        if (r.next_checkpoint == 0) {
            r.lap += 1;
            state.score += 120;
        }
    }

    if (r.lap >= 3) {
        state.completed = true;
        state.victory = true;
        state.score += std::max(0, 300 - static_cast<int>(r.elapsed * 10.0f));
    }
}

void tick_tic_tac_toe(MiniGameState &state, const InputState &input, float dt) {
    auto &ttt = state.tictactoe;
    state.input_cooldown = std::max(0.0f, state.input_cooldown - dt);

    const int cx = ttt.cursor % 3;
    const int cy = ttt.cursor / 3;
    int nx = cx;
    int ny = cy;
    if (state.input_cooldown <= 0.0f) {
        if (input.move.x > 0.6f) {
            nx += 1;
        } else if (input.move.x < -0.6f) {
            nx -= 1;
        } else if (input.move.y > 0.6f) {
            ny -= 1;
        } else if (input.move.y < -0.6f) {
            ny += 1;
        }
        nx = std::clamp(nx, 0, 2);
        ny = std::clamp(ny, 0, 2);
        if (nx != cx || ny != cy) {
            ttt.cursor = ny * 3 + nx;
            state.input_cooldown = 0.12f;
        }
    }

    if (!input.interact_pressed) {
        return;
    }
    if (ttt.board[static_cast<size_t>(ttt.cursor)] != 0) {
        return;
    }

    ttt.board[static_cast<size_t>(ttt.cursor)] = 1;
    ttt.turns += 1;
    int winner = tic_tac_toe_winner(ttt.board);
    if (winner != 0) {
        ttt.winner = static_cast<uint8_t>(winner);
        state.completed = true;
        state.victory = (winner == 1);
        state.score = state.victory ? 100 : 0;
        return;
    }
    if (ttt.turns >= 9) {
        state.completed = true;
        state.victory = false;
        state.score = 50;
        return;
    }

    std::array<int, 9> choices{};
    int choice_count = 0;
    for (int i = 0; i < 9; ++i) {
        if (ttt.board[static_cast<size_t>(i)] == 0) {
            choices[static_cast<size_t>(choice_count++)] = i;
        }
    }
    if (choice_count > 0) {
        const int ai_index = choices[static_cast<size_t>(lcg_next(state.seed) % static_cast<uint32_t>(choice_count))];
        ttt.board[static_cast<size_t>(ai_index)] = 2;
        ttt.turns += 1;
    }

    winner = tic_tac_toe_winner(ttt.board);
    if (winner != 0) {
        ttt.winner = static_cast<uint8_t>(winner);
        state.completed = true;
        state.victory = (winner == 1);
        state.score = state.victory ? 100 : 0;
        return;
    }
    if (ttt.turns >= 9) {
        state.completed = true;
        state.victory = false;
        state.score = 50;
    }
}
}

const char *minigame_name(MiniGameType type) {
    switch (type) {
    case MiniGameType::Snake:
        return "VOXEL SNAKE";
    case MiniGameType::Golf:
        return "VOXEL GOLF";
    case MiniGameType::Tetris:
        return "VOXEL TETRIS";
    case MiniGameType::Racing:
        return "VOXEL RACING";
    case MiniGameType::TicTacToe:
        return "TIC TAC TOE";
    default:
        return "MINIGAME";
    }
}

void minigame_begin(MiniGameState &state, MiniGameType type, uint32_t seed) {
    state = MiniGameState{};
    state.type = type;
    state.seed = (seed == 0) ? 1u : seed;
    state.active = true;

    switch (type) {
    case MiniGameType::Snake:
        begin_snake(state);
        break;
    case MiniGameType::Golf:
        begin_golf(state);
        break;
    case MiniGameType::Tetris:
        begin_tetris(state);
        break;
    case MiniGameType::Racing:
        begin_racing(state);
        break;
    case MiniGameType::TicTacToe:
        begin_tic_tac_toe(state);
        break;
    default:
        break;
    }
}

void minigame_tick(MiniGameState &state, const InputState &input, float dt) {
    if (!state.active || state.completed || dt <= 0.0f) {
        return;
    }

    state.input_cooldown = std::max(0.0f, state.input_cooldown - dt);

    switch (state.type) {
    case MiniGameType::Snake:
        tick_snake(state, input, dt);
        break;
    case MiniGameType::Golf:
        tick_golf(state, input, dt);
        break;
    case MiniGameType::Tetris:
        tick_tetris(state, input, dt);
        break;
    case MiniGameType::Racing:
        tick_racing(state, input, dt);
        break;
    case MiniGameType::TicTacToe:
        tick_tic_tac_toe(state, input, dt);
        break;
    default:
        break;
    }
}

std::string minigame_status_text(const MiniGameState &state) {
    char buffer[256]{};
    switch (state.type) {
    case MiniGameType::Snake:
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s  SCORE %d  LEN %d",
            minigame_name(state.type),
            state.score,
            state.snake.length);
        break;
    case MiniGameType::Golf:
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s  STROKES %d  SCORE %d",
            minigame_name(state.type),
            state.golf.strokes,
            state.score);
        break;
    case MiniGameType::Tetris:
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s  SCORE %d",
            minigame_name(state.type),
            state.score);
        break;
    case MiniGameType::Racing:
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s  LAP %d/3  SCORE %d",
            minigame_name(state.type),
            state.racing.lap,
            state.score);
        break;
    case MiniGameType::TicTacToe:
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s  TURNS %d",
            minigame_name(state.type),
            state.tictactoe.turns);
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "MINIGAME");
        break;
    }

    std::string out(buffer);
    if (state.completed) {
        out += state.victory ? "  [WIN]" : "  [DONE]";
    }
    return out;
}
