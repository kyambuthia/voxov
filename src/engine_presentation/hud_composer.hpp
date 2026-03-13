#pragma once

#include "engine_presentation/presentation_snapshot.hpp"
#include "engine_render/render_types.hpp"

class HudComposer {
public:
  void compose(const RuntimeHudSnapshot &snapshot, RenderStats &render_stats,
               RenderScene &scene);

private:
  uint64_t last_state_hash_ = 0;
  bool has_last_state_hash_ = false;
};
