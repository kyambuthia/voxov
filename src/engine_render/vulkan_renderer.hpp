#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>

#include "engine_render/renderer.hpp"

class VulkanRenderer {
public:
    void init(void *window_handle);
    void shutdown();
    void begin_frame(const RenderFrameContext &ctx);
    void render_world();
    void end_frame();

private:
    void create_instance();
    void create_surface(GLFWwindow *window);
    void pick_physical_device();
    void create_device();
    void create_swapchain();
    void create_render_passes();
    void create_framebuffers();
    void create_pipeline();
    void create_offscreen_targets();
    void create_postprocess_pipeline();
    void create_postprocess_descriptors();
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();
    void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);

    GLFWwindow *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    uint32_t graphics_family = 0;
    uint32_t present_family = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent{};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkRenderPass offscreen_render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkImage offscreen_image = VK_NULL_HANDLE;
    VkDeviceMemory offscreen_memory = VK_NULL_HANDLE;
    VkImageView offscreen_view = VK_NULL_HANDLE;
    VkFramebuffer offscreen_framebuffer = VK_NULL_HANDLE;
    VkSampler offscreen_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout post_desc_layout = VK_NULL_HANDLE;
    VkDescriptorPool post_desc_pool = VK_NULL_HANDLE;
    VkDescriptorSet post_desc_set = VK_NULL_HANDLE;
    VkPipelineLayout post_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline post_pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;

    std::vector<VkSemaphore> image_available;
    std::vector<VkSemaphore> render_finished;
    std::vector<VkFence> in_flight;
    uint32_t frame_index = 0;
};
