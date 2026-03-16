#pragma once

#include "engine_presentation/presentation_snapshot.hpp"
#include "engine_render/render_types.hpp"

class DebugSceneBuilder {
public:
  RenderMesh build(const RuntimeDebugSceneSnapshot &snapshot) const;
};
