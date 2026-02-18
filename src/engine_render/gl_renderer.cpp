#include "engine_render/gl_renderer.hpp"

#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

void GLRenderer::init(void *window_handle) {
    window = static_cast<GLFWwindow *>(window_handle);
    glfwMakeContextCurrent(window);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (ImGui_ImplGlfw_InitForOpenGL(window, true) && ImGui_ImplOpenGL3_Init("#version 130")) {
        imgui_ready = true;
    }
}

void GLRenderer::shutdown() {
    if (imgui_ready) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        imgui_ready = false;
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }
    scene = RenderScene{};
}

void GLRenderer::upload_scene(const RenderScene &new_scene) {
    scene = new_scene;
}

void GLRenderer::update_dynamic_meshes(const RenderMesh &debug_world, const RenderMesh &debug_screen) {
    scene.debug_world = debug_world;
    scene.debug_screen = debug_screen;
}

void GLRenderer::draw_mesh(const RenderMesh &mesh) const {
    glBegin(GL_TRIANGLES);
    for (uint32_t idx : mesh.indices) {
        const RenderVertex &v = mesh.vertices[idx];
        glColor3f(v.color.r, v.color.g, v.color.b);
        glVertex3f(v.position.x, v.position.y, v.position.z);
    }
    glEnd();
}

void GLRenderer::begin_frame(const RenderFrameContext &ctx, const RenderStats &stats) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (height <= 0) {
        height = 1;
    }

    glViewport(0, 0, width, height);
    glClearColor(0.08f, 0.1f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const uint32_t view_count = std::max(1u, std::min(ctx.view_count, 2u));
    for (uint32_t i = 0; i < view_count; ++i) {
        const RenderView &view = ctx.views[i];
        const int vx = static_cast<int>(view.viewport.x * static_cast<float>(width));
        const int vy = static_cast<int>(view.viewport.y * static_cast<float>(height));
        const int vw = std::max(1, static_cast<int>(view.viewport.z * static_cast<float>(width)));
        const int vh = std::max(1, static_cast<int>(view.viewport.w * static_cast<float>(height)));

        glViewport(vx, vy, vw, vh);

        glm::mat4 p = glm::perspective(
            view.camera.fov_y_radians,
            static_cast<float>(vw) / static_cast<float>(vh),
            view.camera.z_near,
            view.camera.z_far);
        glm::mat4 v = view.camera.view();

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(glm::value_ptr(p));
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(glm::value_ptr(v));

        for (const RenderMesh &mesh : scene.opaque_meshes) {
            draw_mesh(mesh);
        }

        draw_mesh(scene.debug_grid);
        if (ctx.debug_xray) {
            glDisable(GL_DEPTH_TEST);
            draw_mesh(scene.debug_world);
            glEnable(GL_DEPTH_TEST);
        } else {
            draw_mesh(scene.debug_world);
        }

        glDisable(GL_DEPTH_TEST);
        draw_mesh(scene.debug_screen);
        glEnable(GL_DEPTH_TEST);
    }

    if (imgui_ready) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("VOXOV Debug")) {
            ImGui::Text("Renderer: OpenGL + Dear ImGui");
            ImGui::Text("FPS: %.1f", static_cast<float>(stats.fps));
            ImGui::Text("CPU ms: %.2f", static_cast<float>(stats.cpu_ms));
            ImGui::Separator();
            ImGui::Text("Hotkeys");
            ImGui::BulletText("F1 Debug Collision");
            ImGui::BulletText("F2 Debug XRay");
            ImGui::BulletText("F3 Collision Only");
            ImGui::BulletText("F4 Freeze Debug");
        }
        ImGui::End();

        if (!stats.menu_text.empty()) {
            ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.92f);
            const ImGuiWindowFlags menu_flags =
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("VOXOV Menu", nullptr, menu_flags)) {
                ImGui::TextUnformatted(stats.menu_text.c_str());
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

void GLRenderer::end_frame() {
    glfwSwapBuffers(window);
}
