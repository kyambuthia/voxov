#pragma once

class RenderContext {
public:
    RenderContext();
    ~RenderContext();

    void init();
    void shutdown();
    void render_frame();
};
