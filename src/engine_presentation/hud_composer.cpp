#include "engine_presentation/hud_composer.hpp"

#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"

#include <cstdint>
#include <string>

namespace {
constexpr uint64_t k_hash_offset = 1469598103934665603ull;
constexpr uint64_t k_hash_prime = 1099511628211ull;

void hash_bytes(uint64_t &hash, const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= k_hash_prime;
  }
}

template <typename T> void hash_value(uint64_t &hash, const T &value) {
  hash_bytes(hash, &value, sizeof(T));
}

void hash_string(uint64_t &hash, const std::string &value) {
  const size_t len = value.size();
  hash_value(hash, len);
  if (!value.empty()) {
    hash_bytes(hash, value.data(), value.size());
  }
}

uint64_t hash_snapshot(const RuntimeHudSnapshot &snapshot) {
  uint64_t hash = k_hash_offset;
  hash_value(hash, snapshot.menu_view.open);
  hash_value(hash, snapshot.menu_view.selected);
  hash_value(hash, snapshot.devhud_enabled);
  hash_value(hash, snapshot.show_minigame_panel);
  hash_value(hash, snapshot.show_hotspot_panel);
  hash_value(hash, snapshot.show_objective_panel);
  hash_value(hash, snapshot.minigame_progress);
  hash_string(hash, snapshot.menu_view.title);
  hash_string(hash, snapshot.menu_view.status);
  for (const std::string &line : snapshot.menu_view.items) {
    hash_string(hash, line);
  }
  for (const std::string &line : snapshot.menu_view.guide_lines) {
    hash_string(hash, line);
  }
  hash_string(hash, snapshot.minigame_title);
  hash_string(hash, snapshot.minigame_status);
  hash_string(hash, snapshot.minigame_objective);
  hash_string(hash, snapshot.minigame_controls);
  hash_string(hash, snapshot.minigame_hint);
  hash_string(hash, snapshot.hotspot_text);
  hash_string(hash, snapshot.objective_status);
  hash_string(hash, snapshot.objective_hint);
  return hash;
}
} // namespace

void HudComposer::compose(const RuntimeHudSnapshot &snapshot,
                          RenderStats &render_stats, RenderScene &scene) {
  const GuiMenuView &menu_view = snapshot.menu_view;
  render_stats.menu_open = menu_view.open;
  render_stats.menu_selected = menu_view.selected;
  render_stats.menu_title = menu_view.title;
  render_stats.menu_items = menu_view.items;
  render_stats.menu_guide = menu_view.guide_lines;
  render_stats.menu_status = menu_view.status;
  render_stats.menu_text.clear();

  const uint64_t state_hash = hash_snapshot(snapshot);
  if (!snapshot.devhud_enabled && has_last_state_hash_ &&
      state_hash == last_state_hash_) {
    return;
  }

  scene.debug_screen = RenderMesh{};
  last_state_hash_ = state_hash;
  has_last_state_hash_ = true;

  auto append_screen_rect = [&](float x0, float y0, float x1, float y1,
                                const glm::vec3 &color) {
    RenderMesh rect{};
    const uint32_t base = 0;
    rect.vertices.push_back({glm::vec3(x0, y0, 0.0f), color});
    rect.vertices.push_back({glm::vec3(x1, y0, 0.0f), color});
    rect.vertices.push_back({glm::vec3(x1, y1, 0.0f), color});
    rect.vertices.push_back({glm::vec3(x0, y1, 0.0f), color});
    rect.indices.insert(rect.indices.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});
    append_mesh(scene.debug_screen, rect);
  };

  const float safe_left = -0.92f;
  const float safe_right = 0.92f;
  const float safe_top = 0.92f;
  const float safe_bottom = -0.90f;

  struct ScreenPanel {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
  };

  auto draw_panel = [&](const ScreenPanel &panel, const glm::vec3 &outer,
                        const glm::vec3 &inner) {
    append_screen_rect(panel.x0, panel.y0, panel.x1, panel.y1, outer);
    append_screen_rect(panel.x0 + 0.01f, panel.y0 - 0.01f, panel.x1 - 0.01f,
                       panel.y1 + 0.01f, inner);
  };

  const ScreenPanel menu_panel{safe_left - 0.02f, safe_top, 0.18f,
                               safe_bottom + 0.12f};
  const ScreenPanel devhud_panel{safe_left - 0.02f, safe_top, 0.14f, 0.12f};
  const ScreenPanel minigame_panel{0.30f, safe_top, safe_right, 0.70f};
  const ScreenPanel hotspot_panel{0.46f, -0.73f, safe_right, safe_bottom};
  const ScreenPanel objective_panel{safe_left - 0.02f, -0.56f, 0.22f,
                                    safe_bottom};

  if (menu_view.open) {
    draw_panel(menu_panel, glm::vec3(0.05f, 0.07f, 0.10f),
               glm::vec3(0.09f, 0.11f, 0.16f));

    float y = menu_panel.y0 - 0.06f;
    if (!menu_view.title.empty()) {
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(menu_view.title, menu_panel.x0 + 0.03f,
                                         y, 0.0082f,
                                         glm::vec3(0.96f, 0.98f, 1.0f)));
      y -= 0.11f;
    }

    for (size_t i = 0; i < menu_view.items.size(); ++i) {
      const bool selected = static_cast<int>(i) == menu_view.selected;
      const std::string line =
          selected ? ("> " + menu_view.items[i]) : ("  " + menu_view.items[i]);
      append_mesh(
          scene.debug_screen,
          build_screen_text_mesh(line, menu_panel.x0 + 0.05f, y, 0.0069f,
                                 selected ? glm::vec3(0.96f, 0.98f, 1.0f)
                                          : glm::vec3(0.86f, 0.91f, 0.98f)));
      y -= 0.095f;
    }

    if (!menu_view.guide_lines.empty()) {
      y -= 0.02f;
      for (const std::string &line : menu_view.guide_lines) {
        append_mesh(scene.debug_screen,
                    build_screen_text_mesh(line, menu_panel.x0 + 0.05f, y,
                                           0.0059f,
                                           glm::vec3(0.80f, 0.88f, 0.97f)));
        y -= 0.072f;
      }
    }

    if (!menu_view.status.empty()) {
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh("STATUS: " + menu_view.status,
                                         menu_panel.x0 + 0.03f,
                                         menu_panel.y1 + 0.05f, 0.0056f,
                                         glm::vec3(0.88f, 0.93f, 0.99f)));
    }
  } else if (snapshot.devhud_enabled) {
    draw_panel(devhud_panel, glm::vec3(0.05f, 0.07f, 0.10f),
               glm::vec3(0.09f, 0.11f, 0.16f));
    append_mesh(scene.debug_screen,
                build_screen_text_mesh(snapshot.devhud_text,
                                       devhud_panel.x0 + 0.03f,
                                       devhud_panel.y0 - 0.06f, 0.0049f,
                                       glm::vec3(0.95f, 0.95f, 0.82f)));
  }

  if (snapshot.show_minigame_panel || snapshot.show_hotspot_panel) {
    if (snapshot.show_minigame_panel) {
      draw_panel(minigame_panel, glm::vec3(0.04f, 0.06f, 0.08f),
                 glm::vec3(0.08f, 0.10f, 0.13f));

      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(snapshot.minigame_title,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.05f, 0.0068f,
                                         glm::vec3(0.98f, 0.98f, 1.0f)));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(snapshot.minigame_status,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.10f, 0.0052f,
                                         glm::vec3(0.89f, 0.95f, 1.0f)));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(snapshot.minigame_objective,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.15f, 0.0048f,
                                         glm::vec3(0.86f, 0.91f, 0.98f)));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(snapshot.minigame_controls,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.21f, 0.0046f,
                                         glm::vec3(0.83f, 0.89f, 0.97f)));
      if (!snapshot.minigame_hint.empty()) {
        append_mesh(scene.debug_screen,
                    build_screen_text_mesh(snapshot.minigame_hint,
                                           minigame_panel.x0 + 0.04f,
                                           minigame_panel.y0 - 0.25f, 0.0045f,
                                           glm::vec3(0.8f, 0.88f, 0.96f)));
      }

      append_screen_rect(minigame_panel.x0 + 0.04f, 0.71f,
                         minigame_panel.x1 - 0.04f, 0.685f,
                         glm::vec3(0.18f, 0.20f, 0.24f));
      const float fill_right =
          (minigame_panel.x0 + 0.04f) +
          ((minigame_panel.x1 - 0.04f) - (minigame_panel.x0 + 0.04f)) *
              snapshot.minigame_progress;
      append_screen_rect(minigame_panel.x0 + 0.04f, 0.71f, fill_right, 0.685f,
                         glm::vec3(0.24f, 0.72f, 0.98f));
    } else {
      draw_panel(hotspot_panel, glm::vec3(0.04f, 0.06f, 0.08f),
                 glm::vec3(0.08f, 0.10f, 0.13f));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(
                      snapshot.hotspot_text, hotspot_panel.x0 + 0.04f,
                      hotspot_panel.y0 - 0.05f, 0.0050f,
                      glm::vec3(0.91f, 0.96f, 1.0f)));
    }
  }

  if (snapshot.show_objective_panel) {
    draw_panel(objective_panel, glm::vec3(0.04f, 0.06f, 0.08f),
               glm::vec3(0.08f, 0.10f, 0.13f));
    append_mesh(scene.debug_screen,
                build_screen_text_mesh(snapshot.objective_status,
                                       objective_panel.x0 + 0.04f,
                                       objective_panel.y0 - 0.05f, 0.0050f,
                                       glm::vec3(0.95f, 0.97f, 1.0f)));
    if (!snapshot.objective_hint.empty()) {
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(snapshot.objective_hint,
                                         objective_panel.x0 + 0.04f,
                                         objective_panel.y0 - 0.11f, 0.0045f,
                                         glm::vec3(0.84f, 0.90f, 0.98f)));
    }
  }
}
