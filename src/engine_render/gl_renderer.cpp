#include "engine_render/gl_renderer.hpp"

#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <glm/gtc/type_ptr.hpp>

void GLRenderer::init(void *window_handle) {
    window = static_cast<GLFWwindow *>(window_handle);
    glfwMakeContextCurrent(window);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

void GLRenderer::shutdown() {
    scene = RenderScene{};
}

void GLRenderer::upload_scene(const RenderScene &new_scene) {
    scene = new_scene;
}

void GLRenderer::update_overlay_text(const RenderMesh &overlay) {
    scene.overlay_text = overlay;
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

void GLRenderer::begin_frame(const RenderFrameContext &ctx, const Camera &camera, const RenderStats &stats) {
    (void)ctx;
    (void)stats;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (height <= 0) {
        height = 1;
    }

    glViewport(0, 0, width, height);
    glClearColor(0.08f, 0.1f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 p = glm::perspective(camera.fov_y_radians, static_cast<float>(width) / static_cast<float>(height), camera.z_near, camera.z_far);
    glm::mat4 v = camera.view();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(p));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(v));

    for (const RenderMesh &mesh : scene.opaque_meshes) {
        draw_mesh(mesh);
    }

    draw_mesh(scene.debug_grid);
    draw_mesh(scene.overlay_text);
}

void GLRenderer::end_frame() {
    glfwSwapBuffers(window);
}
