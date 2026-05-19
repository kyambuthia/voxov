#include "engine_render/debug_text.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

namespace {

using GlyphRows = std::array<uint8_t, 7>;

struct RasterGlyph {
  int width = 0;
  int height = 0;
  int offset_x = 0;
  int offset_y = 0;
  int advance = 0;
  std::vector<uint8_t> bitmap;
};

struct RasterFont {
  bool loaded = false;
  int pixel_height = 32;
  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  int line_height = 32;
  int mono_advance = 0;
  std::array<RasterGlyph, 128> glyphs{};
};

constexpr int k_font_bitmap_height = 32;
constexpr uint8_t k_font_alpha_threshold = 84;

const std::unordered_map<char, GlyphRows> kGlyphs = {
    {'A', {0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'>', {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {':', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}};

std::filesystem::path executable_directory() {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  std::array<char, 4096> path{};
  const unsigned long len = GetModuleFileNameA(
      nullptr, path.data(), static_cast<unsigned long>(path.size()));
  if (len > 0 && len < path.size()) {
    return fs::path(std::string(path.data(), len)).parent_path();
  }
  return fs::path(".");
#elif defined(__EMSCRIPTEN__)
  return fs::path("/");
#elif defined(__linux__)
  std::array<char, 4096> path{};
  const ssize_t len = readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (len > 0) {
    path[static_cast<size_t>(len)] = '\0';
    return fs::path(path.data()).parent_path();
  }
  return fs::path(".");
#elif defined(__APPLE__)
  std::array<char, 4096> path{};
  uint32_t size = static_cast<uint32_t>(path.size());
  if (_NSGetExecutablePath(path.data(), &size) == 0) {
    return fs::path(path.data()).parent_path();
  }
  return fs::path(".");
#else
  return fs::path(".");
#endif
}

std::vector<std::filesystem::path>
candidate_font_paths(const char *font_filename) {
  namespace fs = std::filesystem;
  std::vector<fs::path> out;
  if (!font_filename || *font_filename == '\0') {
    return out;
  }

  const fs::path rel = fs::path("assets") / "fonts" / font_filename;
  fs::path prefix(".");
  for (int i = 0; i < 6; ++i) {
    out.push_back(prefix / rel);
    prefix /= "..";
  }

  fs::path exe_prefix = executable_directory();
  for (int i = 0; i < 6; ++i) {
    out.push_back(exe_prefix / rel);
    exe_prefix /= "..";
  }

  return out;
}

bool load_file_bytes(const std::filesystem::path &path,
                     std::vector<uint8_t> &out_bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::streamsize size = file.tellg();
  if (size <= 0) {
    return false;
  }
  file.seekg(0, std::ios::beg);
  out_bytes.resize(static_cast<size_t>(size));
  file.read(reinterpret_cast<char *>(out_bytes.data()), size);
  return file.good();
}

bool load_raster_font(RasterFont &font) {
  std::vector<uint8_t> font_bytes;
  bool found_font = false;
  const std::vector<std::filesystem::path> candidates =
      candidate_font_paths("IBMPlexMono-Regular.ttf");
  for (const std::filesystem::path &path : candidates) {
    if (load_file_bytes(path, font_bytes)) {
      found_font = true;
      spdlog::info("DebugText: loaded font from {}", path.generic_string());
      break;
    }
  }
  if (!found_font) {
    std::string searched;
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (i > 0) {
        searched += ", ";
      }
      searched += candidates[i].generic_string();
    }
    spdlog::warn(
        "DebugText: font IBMPlexMono-Regular.ttf not found; searched {}",
        searched);
    return false;
  }

  stbtt_fontinfo info{};
  if (stbtt_InitFont(&info, font_bytes.data(), 0) == 0) {
    return false;
  }

  const float scale =
      stbtt_ScaleForPixelHeight(&info, static_cast<float>(font.pixel_height));
  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
  font.ascent = static_cast<int>(std::round(ascent * scale));
  font.descent = static_cast<int>(std::round(descent * scale));
  font.line_gap = static_cast<int>(std::round(line_gap * scale));
  font.line_height = std::max(1, font.ascent - font.descent + font.line_gap);

  for (int code = 32; code < 127; ++code) {
    RasterGlyph glyph{};
    int advance = 0;
    int left_bearing = 0;
    stbtt_GetCodepointHMetrics(&info, code, &advance, &left_bearing);
    glyph.advance = std::max(1, static_cast<int>(std::round(advance * scale)));
    font.mono_advance = std::max(font.mono_advance, glyph.advance);

    int xoff = 0;
    int yoff = 0;
    unsigned char *bitmap = stbtt_GetCodepointBitmap(
        &info, 0.0f, scale, code, &glyph.width, &glyph.height, &xoff, &yoff);
    glyph.offset_x = xoff;
    glyph.offset_y = yoff;
    if (bitmap && glyph.width > 0 && glyph.height > 0) {
      glyph.bitmap.assign(
          bitmap, bitmap + static_cast<size_t>(glyph.width * glyph.height));
    }
    stbtt_FreeBitmap(bitmap, nullptr);
    font.glyphs[static_cast<size_t>(code)] = std::move(glyph);
  }

  if (font.mono_advance <= 0) {
    font.mono_advance = font.pixel_height / 2;
  }
  font.loaded = true;
  return true;
}

const RasterFont *debug_font() {
  static RasterFont font{};
  static std::once_flag once;
  std::call_once(once, []() {
    font.pixel_height = k_font_bitmap_height;
    load_raster_font(font);
  });
  return font.loaded ? &font : nullptr;
}

void add_pixel_rect(RenderMesh &mesh, const glm::vec3 &origin,
                    const glm::vec3 &right, const glm::vec3 &up, float x,
                    float y, float width, float height, float cell_size,
                    const glm::vec3 &color) {
  const glm::vec3 p0 = origin + right * (x * cell_size) - up * (y * cell_size);
  const glm::vec3 p1 = p0 + right * (width * cell_size);
  const glm::vec3 p2 = p1 - up * (height * cell_size);
  const glm::vec3 p3 = p0 - up * (height * cell_size);

  uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({p0, color});
  mesh.vertices.push_back({p1, color});
  mesh.vertices.push_back({p2, color});
  mesh.vertices.push_back({p3, color});
  mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start,
                                           start + 2, start + 3});
}

void add_screen_pixel_rect_ndc(RenderMesh &mesh, float origin_x_ndc,
                               float origin_y_ndc, float x, float y,
                               float width, float height, float cell_size_ndc,
                               const glm::vec3 &color) {
  const float x0 = origin_x_ndc + x * cell_size_ndc;
  const float y0 = origin_y_ndc - y * cell_size_ndc;
  const float x1 = x0 + width * cell_size_ndc;
  const float y1 = y0 - height * cell_size_ndc;

  const glm::vec3 p0(x0, y0, 0.0f);
  const glm::vec3 p1(x1, y0, 0.0f);
  const glm::vec3 p2(x1, y1, 0.0f);
  const glm::vec3 p3(x0, y1, 0.0f);

  uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({p0, color});
  mesh.vertices.push_back({p1, color});
  mesh.vertices.push_back({p2, color});
  mesh.vertices.push_back({p3, color});
  mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start,
                                           start + 2, start + 3});
}

GlyphRows glyph_for(char c) {
  char uc = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  auto it = kGlyphs.find(uc);
  if (it != kGlyphs.end()) {
    return it->second;
  }
  return kGlyphs.at(' ');
}

void append_raster_glyph_world(RenderMesh &mesh, const RasterFont &font, char c,
                               const glm::vec3 &origin, const glm::vec3 &right,
                               const glm::vec3 &up, float pen_x, float pen_y,
                               float cell_size, const glm::vec3 &color) {
  const unsigned char code = static_cast<unsigned char>(c);
  if (code >= font.glyphs.size()) {
    return;
  }
  const RasterGlyph &glyph = font.glyphs[code];
  if (glyph.width <= 0 || glyph.height <= 0 || glyph.bitmap.empty()) {
    return;
  }

  const float glyph_x = pen_x + static_cast<float>(glyph.offset_x);
  const float glyph_y =
      pen_y + static_cast<float>(font.ascent + glyph.offset_y);
  for (int row = 0; row < glyph.height; ++row) {
    int run_start = -1;
    for (int col = 0; col <= glyph.width; ++col) {
      const bool on =
          col < glyph.width &&
          glyph.bitmap[static_cast<size_t>(row * glyph.width + col)] >=
              k_font_alpha_threshold;
      if (on && run_start < 0) {
        run_start = col;
      } else if (!on && run_start >= 0) {
        add_pixel_rect(
            mesh, origin, right, up, glyph_x + static_cast<float>(run_start),
            glyph_y + static_cast<float>(row),
            static_cast<float>(col - run_start), 1.0f, cell_size, color);
        run_start = -1;
      }
    }
  }
}

void append_raster_glyph_screen(RenderMesh &mesh, const RasterFont &font,
                                char c, float origin_x_ndc, float origin_y_ndc,
                                float pen_x, float pen_y, float cell_size_ndc,
                                const glm::vec3 &color) {
  const unsigned char code = static_cast<unsigned char>(c);
  if (code >= font.glyphs.size()) {
    return;
  }
  const RasterGlyph &glyph = font.glyphs[code];
  if (glyph.width <= 0 || glyph.height <= 0 || glyph.bitmap.empty()) {
    return;
  }

  const float glyph_x = pen_x + static_cast<float>(glyph.offset_x);
  const float glyph_y =
      pen_y + static_cast<float>(font.ascent + glyph.offset_y);
  for (int row = 0; row < glyph.height; ++row) {
    int run_start = -1;
    for (int col = 0; col <= glyph.width; ++col) {
      const bool on =
          col < glyph.width &&
          glyph.bitmap[static_cast<size_t>(row * glyph.width + col)] >=
              k_font_alpha_threshold;
      if (on && run_start < 0) {
        run_start = col;
      } else if (!on && run_start >= 0) {
        add_screen_pixel_rect_ndc(mesh, origin_x_ndc, origin_y_ndc,
                                  glyph_x + static_cast<float>(run_start),
                                  glyph_y + static_cast<float>(row),
                                  static_cast<float>(col - run_start), 1.0f,
                                  cell_size_ndc, color);
        run_start = -1;
      }
    }
  }
}

} // namespace

RenderMesh build_camera_text_mesh(const Camera &camera,
                                  const std::string &text) {
  RenderMesh mesh;

  const float cell_size = 0.035f;
  const float char_width = 6.0f;
  const float line_height = 8.0f;

  glm::vec3 forward = camera.forward();
  glm::vec3 right = camera.right();
  glm::vec3 up = camera.up();

  glm::vec3 anchor =
      camera.transform.position + forward * 1.3f + up * 0.5f - right * 0.75f;
  glm::vec3 color = glm::vec3(0.95f, 0.95f, 0.82f);

  float pen_x = 0.0f;
  float pen_y = 0.0f;
  const RasterFont *font = debug_font();
  if (font) {
    const float target_height = cell_size * 7.0f;
    const float font_cell_size =
        target_height / static_cast<float>(std::max(1, font->pixel_height));

    for (char c : text) {
      if (c == '\n') {
        pen_x = 0.0f;
        pen_y += static_cast<float>(font->line_height);
        continue;
      }
      append_raster_glyph_world(mesh, *font, c, anchor, right, up, pen_x, pen_y,
                                font_cell_size, color);
      pen_x += static_cast<float>(font->mono_advance);
    }
    return mesh;
  }

  for (char c : text) {
    if (c == '\n') {
      pen_x = 0.0f;
      pen_y += line_height;
      continue;
    }

    GlyphRows glyph = glyph_for(c);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        const bool on = (glyph[row] & (1 << (4 - col))) != 0;
        if (!on) {
          continue;
        }
        add_pixel_rect(mesh, anchor, right, up, pen_x + static_cast<float>(col),
                       pen_y + static_cast<float>(row), 1.0f, 1.0f, cell_size,
                       color);
      }
    }

    pen_x += char_width;
  }

  return mesh;
}

RenderMesh build_screen_text_mesh(const std::string &text, float origin_x_ndc,
                                  float origin_y_ndc, float cell_size_ndc,
                                  const glm::vec3 &color,
                                  float line_spacing_scale) {
  RenderMesh mesh;
  if (text.empty() || cell_size_ndc <= 0.0f) {
    return mesh;
  }

  float pen_x = 0.0f;
  float pen_y = 0.0f;
  const float char_width = 6.0f;
  const float line_height = 8.0f * std::max(1.0f, line_spacing_scale);
  const RasterFont *font = debug_font();
  if (font) {
    const float target_height_ndc = cell_size_ndc * 7.0f;
    const float font_cell_size_ndc =
        target_height_ndc / static_cast<float>(std::max(1, font->pixel_height));
    for (char c : text) {
      if (c == '\n') {
        pen_x = 0.0f;
        pen_y += static_cast<float>(font->line_height) *
                 std::max(1.0f, line_spacing_scale);
        continue;
      }
      append_raster_glyph_screen(mesh, *font, c, origin_x_ndc, origin_y_ndc,
                                 pen_x, pen_y, font_cell_size_ndc, color);
      pen_x += static_cast<float>(font->mono_advance);
    }
    return mesh;
  }

  for (char c : text) {
    if (c == '\n') {
      pen_x = 0.0f;
      pen_y += line_height;
      continue;
    }

    GlyphRows glyph = glyph_for(c);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        const bool on = (glyph[row] & (1 << (4 - col))) != 0;
        if (!on) {
          continue;
        }
        add_screen_pixel_rect_ndc(
            mesh, origin_x_ndc, origin_y_ndc, pen_x + static_cast<float>(col),
            pen_y + static_cast<float>(row), 1.0f, 1.0f, cell_size_ndc, color);
      }
    }
    pen_x += char_width;
  }

  return mesh;
}
