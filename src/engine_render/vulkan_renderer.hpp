#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/glm.hpp>

#include "engine_render/render_backend.hpp"

class VulkanRenderer : public IRenderBackend {
public:
    void init(void *window_handle) override;
    void shutdown() override;
    void upload_scene(const RenderScene &scene) override;
    void update_overlay_text(const RenderMesh &overlay) override;
    void begin_frame(const RenderFrameContext &ctx, const Camera &camera, const RenderStats &stats) override;
    void end_frame() override;

private:
    void create_instance();
    void setup_debug_messenger();
    void destroy_debug_messenger();
    void create_surface(GLFWwindow *window);
    void pick_physical_device();
    void create_device();
    void create_swapchain();
    void create_render_passes();
    void create_framebuffers();
    void create_pipeline();
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();
    void create_scene_buffers();
    void create_mesh_buffers(
        const std::vector<RenderVertex> &vertices,
        const std::vector<uint32_t> &indices,
        VkBuffer &out_vertex_buffer,
        VkDeviceMemory &out_vertex_memory,
        VkBuffer &out_index_buffer,
        VkDeviceMemory &out_index_memory);
    void destroy_mesh_buffers(
        VkBuffer &inout_vertex_buffer,
        VkDeviceMemory &inout_vertex_memory,
        VkBuffer &inout_index_buffer,
        VkDeviceMemory &inout_index_memory);
    void destroy_scene_buffers();
    void create_depth_resources();
    void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);

    GLFWwindow *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    uint32_t graphics_family = 0;
    uint32_t present_family = 0;

    bool enable_validation_layers = false;
    const char *validation_layers[1] = { "VK_LAYER_KHRONOS_validation" };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent{};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView depth_image_view = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;

    std::vector<VkSemaphore> image_available;
    std::vector<VkSemaphore> render_finished;
    std::vector<VkFence> in_flight;
    std::vector<VkFence> images_in_flight;
    RenderScene scene_data;
    std::vector<RenderVertex> static_vertices;
    std::vector<uint32_t> static_indices;
    std::vector<RenderVertex> overlay_vertices;
    std::vector<uint32_t> overlay_indices;
    VkBuffer static_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory static_vertex_memory = VK_NULL_HANDLE;
    VkBuffer static_index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory static_index_memory = VK_NULL_HANDLE;
    VkBuffer overlay_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory overlay_vertex_memory = VK_NULL_HANDLE;
    VkBuffer overlay_index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory overlay_index_memory = VK_NULL_HANDLE;
    uint32_t static_index_count = 0;
    uint32_t overlay_index_count = 0;
    glm::mat4 current_view_proj = glm::mat4(1.0f);
    uint32_t current_image_index = 0;
    uint32_t frame_index = 0;
};
