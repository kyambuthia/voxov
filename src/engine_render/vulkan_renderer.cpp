#include "engine_render/vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct PushConstants {
    glm::mat4 view_proj;
};

const char *vk_result_string(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    default: return "VK_UNKNOWN_ERROR";
    }
}

VkShaderModule create_shader(VkDevice device, const std::vector<char> &code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }
    return module;
}

std::vector<char> read_file_with_fallback(const std::vector<std::string> &candidates) {
    for (const std::string &path : candidates) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            continue;
        }

        size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(size));
        return buffer;
    }

    throw std::runtime_error("failed to open shader from known paths");
}
}

void VulkanRenderer::init(void *window_handle) {
    window = static_cast<GLFWwindow *>(window_handle);
    enable_validation_layers = true;

    create_instance();
    setup_debug_messenger();
    create_surface(window);
    pick_physical_device();
    create_device();
    create_command_pool();
    create_swapchain();
    create_depth_resources();
    create_render_passes();
    create_framebuffers();
    create_pipeline();
    create_command_buffers();
    create_sync_objects();
}

void VulkanRenderer::shutdown() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    destroy_scene_buffers();

    for (size_t i = 0; i < image_available.size(); ++i) {
        if (image_available[i]) {
            vkDestroySemaphore(device, image_available[i], nullptr);
        }
    }
    for (size_t i = 0; i < render_finished.size(); ++i) {
        if (render_finished[i]) {
            vkDestroySemaphore(device, render_finished[i], nullptr);
        }
    }
    for (size_t i = 0; i < in_flight.size(); ++i) {
        if (in_flight[i]) {
            vkDestroyFence(device, in_flight[i], nullptr);
        }
    }

    cleanup_swapchain_resources();

    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }

    destroy_debug_messenger();

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

void VulkanRenderer::upload_scene(const RenderScene &scene) {
    scene_data = scene;
    static_vertices.clear();
    static_indices.clear();
    debug_world_vertices = scene.debug_world.vertices;
    debug_world_indices = scene.debug_world.indices;
    debug_screen_vertices = scene.debug_screen.vertices;
    debug_screen_indices = scene.debug_screen.indices;

    auto append_mesh = [&](const RenderMesh &mesh) {
        uint32_t base = static_cast<uint32_t>(static_vertices.size());
        static_vertices.insert(static_vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (uint32_t idx : mesh.indices) {
            static_indices.push_back(base + idx);
        }
    };

    for (const RenderMesh &mesh : scene.opaque_meshes) {
        append_mesh(mesh);
    }
    append_mesh(scene.debug_grid);

    static_index_count = static_cast<uint32_t>(static_indices.size());
    debug_world_index_count = static_cast<uint32_t>(debug_world_indices.size());
    debug_screen_index_count = static_cast<uint32_t>(debug_screen_indices.size());
    create_scene_buffers();
}

void VulkanRenderer::update_dynamic_meshes(const RenderMesh &debug_world, const RenderMesh &debug_screen) {
    debug_world_vertices = debug_world.vertices;
    debug_world_indices = debug_world.indices;
    debug_world_index_count = static_cast<uint32_t>(debug_world_indices.size());
    debug_screen_vertices = debug_screen.vertices;
    debug_screen_indices = debug_screen.indices;
    debug_screen_index_count = static_cast<uint32_t>(debug_screen_indices.size());

    scene_data.debug_world = debug_world;
    scene_data.debug_screen = debug_screen;
    if (device == VK_NULL_HANDLE) {
        return;
    }
    create_scene_buffers();
}

void VulkanRenderer::begin_frame(const RenderFrameContext &ctx, const RenderStats &stats) {
    (void)stats;
    current_debug_xray = ctx.debug_xray;
    current_view_count = std::max(1u, std::min(ctx.view_count, 2u));
    for (uint32_t i = 0; i < current_view_count; ++i) {
        current_viewports[i] = ctx.views[i].viewport;
        const Camera &camera = ctx.views[i].camera;
        current_view_proj[i] = camera.projection(ctx.aspect_ratio) * camera.view();
    }
}

void VulkanRenderer::end_frame() {
    if (swapchain == VK_NULL_HANDLE || static_index_count == 0) {
        return;
    }

    vkWaitForFences(device, 1, &in_flight[frame_index], VK_TRUE, UINT64_MAX);

    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        image_available[frame_index],
        VK_NULL_HANDLE,
        &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "vkAcquireNextImageKHR failed: %s\n", vk_result_string(result));
        return;
    }

    if (images_in_flight[image_index] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &images_in_flight[image_index], VK_TRUE, UINT64_MAX);
    }
    images_in_flight[image_index] = in_flight[frame_index];

    vkResetFences(device, 1, &in_flight[frame_index]);
    vkResetCommandBuffer(command_buffers[image_index], 0);
    current_image_index = image_index;
    record_command_buffer(command_buffers[image_index], image_index);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available[frame_index];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffers[image_index];
    VkSemaphore signal_semaphore = render_finished[image_index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &signal_semaphore;

    if (vkQueueSubmit(graphics_queue, 1, &submit, in_flight[frame_index]) != VK_SUCCESS) {
        std::fprintf(stderr, "vkQueueSubmit failed\n");
        return;
    }

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &signal_semaphore;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &image_index;
    result = vkQueuePresentKHR(present_queue, &present);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    } else if (result != VK_SUCCESS) {
        std::fprintf(stderr, "vkQueuePresentKHR failed: %s\n", vk_result_string(result));
    }

    frame_index = (frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::create_instance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "VOXOV";
    app.apiVersion = VK_API_VERSION_1_3;

    uint32_t ext_count = 0;
    const char **exts = glfwGetRequiredInstanceExtensions(&ext_count);
    if (!exts || ext_count == 0) {
        throw std::runtime_error("glfwGetRequiredInstanceExtensions returned no extensions");
    }

    std::vector<const char *> extensions(exts, exts + ext_count);
    if (enable_validation_layers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

    if (enable_validation_layers) {
        bool found = false;
        for (const auto &layer : layers) {
            if (std::strcmp(layer.layerName, validation_layers[0]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::fprintf(stderr, "Validation layer not found; continuing without it\n");
            enable_validation_layers = false;
        }
    }

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    if (enable_validation_layers) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = validation_layers;
    }

    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance");
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *user_data) {
    (void)type;
    (void)user_data;

    const char *sev =
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" :
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARN" :
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) ? "INFO" : "VERBOSE";
    std::fprintf(stderr, "Vulkan [%s]: %s\n", sev, callback_data->pMessage);
    return VK_FALSE;
}

void VulkanRenderer::setup_debug_messenger() {
    if (!enable_validation_layers) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_callback;

    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create_fn && create_fn(instance, &info, nullptr, &debug_messenger) != VK_SUCCESS) {
        std::fprintf(stderr, "Failed to create debug messenger\n");
    }
}

void VulkanRenderer::destroy_debug_messenger() {
    if (!debug_messenger) {
        return;
    }

    auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_fn) {
        destroy_fn(instance, debug_messenger, nullptr);
    }
    debug_messenger = VK_NULL_HANDLE;
}

void VulkanRenderer::create_surface(GLFWwindow *window_handle) {
    if (glfwCreateWindowSurface(instance, window_handle, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create surface");
    }
}

void VulkanRenderer::pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("no Vulkan devices");
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    physical_device = devices[0];
}

void VulkanRenderer::create_device() {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, families.data());

    graphics_family = 0;
    present_family = 0;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_family = i;
        }

        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present);
        if (present) {
            present_family = i;
        }
    }

    float priority = 1.0f;
    std::set<uint32_t> unique_families = { graphics_family, present_family };
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    for (uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queue_infos.push_back(q);
    }

    const char *exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
    info.pQueueCreateInfos = queue_infos.data();
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = exts;

    if (vkCreateDevice(physical_device, &info, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create device");
    }

    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
    vkGetDeviceQueue(device, present_family, 0, &present_queue);
}

void VulkanRenderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps);

    if (caps.currentExtent.width != UINT32_MAX) {
        swapchain_extent = caps.currentExtent;
    } else {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        swapchain_extent.width = static_cast<uint32_t>(std::max(w, 1));
        swapchain_extent.height = static_cast<uint32_t>(std::max(h, 1));
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
    if (format_count == 0) {
        throw std::runtime_error("surface reported zero formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data());

    swapchain_format = formats[0].format;
    VkColorSpaceKHR color_space = formats[0].colorSpace;
    for (const auto &f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapchain_format = f.format;
            color_space = f.colorSpace;
            break;
        }
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = image_count;
    info.imageFormat = swapchain_format;
    info.imageColorSpace = color_space;
    info.imageExtent = swapchain_extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queue_family_indices[] = { graphics_family, present_family };
    if (graphics_family != present_family) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = queue_family_indices;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain");
    }

    vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
    swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images.data());

    swapchain_image_views.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = swapchain_images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = swapchain_format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &view, nullptr, &swapchain_image_views[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swapchain image view");
        }
    }
}

void VulkanRenderer::create_depth_resources() {
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.extent = { swapchain_extent.width, swapchain_extent.height, 1 };
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.format = depth_format;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &image, nullptr, &depth_image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth image");
    }

    VkMemoryRequirements mem{};
    vkGetImageMemoryRequirements(device, depth_image, &mem);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = mem.size;
    alloc.memoryTypeIndex = find_memory_type(mem.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &alloc, nullptr, &depth_memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate depth memory");
    }

    vkBindImageMemory(device, depth_image, depth_memory, 0);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = depth_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = depth_format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &view, nullptr, &depth_image_view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth image view");
    }
}

void VulkanRenderer::create_render_passes() {
    VkAttachmentDescription color{};
    color.format = swapchain_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = depth_format;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    std::array<VkAttachmentDescription, 2> attachments = { color, depth };

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;

    if (vkCreateRenderPass(device, &info, nullptr, &render_pass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass");
    }
}

void VulkanRenderer::create_framebuffers() {
    framebuffers.resize(swapchain_image_views.size());
    for (size_t i = 0; i < swapchain_image_views.size(); ++i) {
        std::array<VkImageView, 2> attachments = { swapchain_image_views[i], depth_image_view };

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = render_pass;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.width = swapchain_extent.width;
        info.height = swapchain_extent.height;
        info.layers = 1;

        if (vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

void VulkanRenderer::create_pipeline() {
    const auto vert = read_file_with_fallback({
        "triangle.vert.spv",
        "../triangle.vert.spv",
        "build/triangle.vert.spv",
        "./build/triangle.vert.spv"
    });
    const auto frag = read_file_with_fallback({
        "triangle.frag.spv",
        "../triangle.frag.spv",
        "build/triangle.frag.spv",
        "./build/triangle.frag.spv"
    });

    VkShaderModule vert_module = create_shader(device, vert);
    VkShaderModule frag_module = create_shader(device, frag);

    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vert_stage, frag_stage };

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(RenderVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(RenderVertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(RenderVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState color_blend{};
    color_blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &color_blend;

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset = 0;
    range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;

    if (vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &msaa;
    info.pDepthStencilState = &depth_stencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic_state;
    info.layout = pipeline_layout;
    info.renderPass = render_pass;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline");
    }

    VkPipelineDepthStencilStateCreateInfo depth_disabled = depth_stencil;
    depth_disabled.depthTestEnable = VK_FALSE;
    depth_disabled.depthWriteEnable = VK_FALSE;
    info.pDepthStencilState = &depth_disabled;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_no_depth) != VK_SUCCESS) {
        throw std::runtime_error("failed to create debug no-depth pipeline");
    }

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);
}

uint32_t VulkanRenderer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem);

    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find memory type");
}

void VulkanRenderer::create_command_pool() {
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = graphics_family;

    if (vkCreateCommandPool(device, &info, nullptr, &command_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

void VulkanRenderer::create_command_buffers() {
    command_buffers.resize(swapchain_images.size());

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = command_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<uint32_t>(command_buffers.size());

    if (vkAllocateCommandBuffers(device, &alloc, command_buffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }
}

void VulkanRenderer::cleanup_swapchain_resources() {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    if (command_pool != VK_NULL_HANDLE && !command_buffers.empty()) {
        vkFreeCommandBuffers(
            device,
            command_pool,
            static_cast<uint32_t>(command_buffers.size()),
            command_buffers.data());
        command_buffers.clear();
    }

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipeline_no_depth != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_no_depth, nullptr);
        pipeline_no_depth = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }

    for (VkFramebuffer fb : framebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }
    framebuffers.clear();

    if (render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass, nullptr);
        render_pass = VK_NULL_HANDLE;
    }

    if (depth_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depth_image_view, nullptr);
        depth_image_view = VK_NULL_HANDLE;
    }
    if (depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, depth_image, nullptr);
        depth_image = VK_NULL_HANDLE;
    }
    if (depth_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, depth_memory, nullptr);
        depth_memory = VK_NULL_HANDLE;
    }

    for (VkImageView view : swapchain_image_views) {
        vkDestroyImageView(device, view, nullptr);
    }
    swapchain_image_views.clear();
    swapchain_images.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::recreate_swapchain() {
    if (device == VK_NULL_HANDLE || window == nullptr) {
        return;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }

    vkDeviceWaitIdle(device);
    cleanup_swapchain_resources();
    create_swapchain();
    create_depth_resources();
    create_render_passes();
    create_framebuffers();
    create_pipeline();
    create_command_buffers();
    if (render_finished.size() != swapchain_images.size()) {
        for (VkSemaphore semaphore : render_finished) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        render_finished.assign(swapchain_images.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (size_t i = 0; i < render_finished.size(); ++i) {
            if (vkCreateSemaphore(device, &sem, nullptr, &render_finished[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to recreate render-finished semaphores");
            }
        }
    }
    images_in_flight.assign(swapchain_images.size(), VK_NULL_HANDLE);
}

void VulkanRenderer::create_sync_objects() {
    image_available.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished.resize(swapchain_images.size());
    in_flight.resize(MAX_FRAMES_IN_FLIGHT);
    images_in_flight.resize(swapchain_images.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo sem{};
    sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(device, &sem, nullptr, &image_available[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fence, nullptr, &in_flight[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create sync objects");
        }
    }
    for (uint32_t i = 0; i < render_finished.size(); ++i) {
        if (vkCreateSemaphore(device, &sem, nullptr, &render_finished[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render-finished semaphore");
        }
    }
}

void VulkanRenderer::destroy_scene_buffers() {
    destroy_mesh_buffers(
        static_vertex_buffer,
        static_vertex_memory,
        static_index_buffer,
        static_index_memory);
    destroy_mesh_buffers(
        debug_world_vertex_buffer,
        debug_world_vertex_memory,
        debug_world_index_buffer,
        debug_world_index_memory);
    destroy_mesh_buffers(
        debug_screen_vertex_buffer,
        debug_screen_vertex_memory,
        debug_screen_index_buffer,
        debug_screen_index_memory);
}

void VulkanRenderer::create_mesh_buffers(
    const std::vector<RenderVertex> &vertices,
    const std::vector<uint32_t> &indices,
    VkBuffer &out_vertex_buffer,
    VkDeviceMemory &out_vertex_memory,
    VkBuffer &out_index_buffer,
    VkDeviceMemory &out_index_memory) {
    VkDeviceSize vb_size = static_cast<VkDeviceSize>(vertices.size() * sizeof(RenderVertex));
    VkBufferCreateInfo vb{};
    vb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vb.size = vb_size;
    vb.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vb.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &vb, nullptr, &out_vertex_buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer");
    }

    VkMemoryRequirements vb_mem{};
    vkGetBufferMemoryRequirements(device, out_vertex_buffer, &vb_mem);
    VkMemoryAllocateInfo vb_alloc{};
    vb_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vb_alloc.allocationSize = vb_mem.size;
    vb_alloc.memoryTypeIndex = find_memory_type(vb_mem.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &vb_alloc, nullptr, &out_vertex_memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex memory");
    }

    void *vb_ptr = nullptr;
    vkMapMemory(device, out_vertex_memory, 0, vb_size, 0, &vb_ptr);
    std::memcpy(vb_ptr, vertices.data(), static_cast<size_t>(vb_size));
    vkUnmapMemory(device, out_vertex_memory);
    vkBindBufferMemory(device, out_vertex_buffer, out_vertex_memory, 0);

    VkDeviceSize ib_size = static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));
    VkBufferCreateInfo ib{};
    ib.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ib.size = ib_size;
    ib.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ib.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ib, nullptr, &out_index_buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer");
    }

    VkMemoryRequirements ib_mem{};
    vkGetBufferMemoryRequirements(device, out_index_buffer, &ib_mem);
    VkMemoryAllocateInfo ib_alloc{};
    ib_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ib_alloc.allocationSize = ib_mem.size;
    ib_alloc.memoryTypeIndex = find_memory_type(ib_mem.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &ib_alloc, nullptr, &out_index_memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate index memory");
    }

    void *ib_ptr = nullptr;
    vkMapMemory(device, out_index_memory, 0, ib_size, 0, &ib_ptr);
    std::memcpy(ib_ptr, indices.data(), static_cast<size_t>(ib_size));
    vkUnmapMemory(device, out_index_memory);
    vkBindBufferMemory(device, out_index_buffer, out_index_memory, 0);
}

void VulkanRenderer::destroy_mesh_buffers(
    VkBuffer &inout_vertex_buffer,
    VkDeviceMemory &inout_vertex_memory,
    VkBuffer &inout_index_buffer,
    VkDeviceMemory &inout_index_memory) {
    if (inout_vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, inout_vertex_buffer, nullptr);
        inout_vertex_buffer = VK_NULL_HANDLE;
    }
    if (inout_vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, inout_vertex_memory, nullptr);
        inout_vertex_memory = VK_NULL_HANDLE;
    }
    if (inout_index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, inout_index_buffer, nullptr);
        inout_index_buffer = VK_NULL_HANDLE;
    }
    if (inout_index_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, inout_index_memory, nullptr);
        inout_index_memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::create_scene_buffers() {
    if (device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device);
    destroy_scene_buffers();

    if (!static_vertices.empty() && !static_indices.empty()) {
        create_mesh_buffers(
            static_vertices,
            static_indices,
            static_vertex_buffer,
            static_vertex_memory,
            static_index_buffer,
            static_index_memory);
    }

    if (!debug_world_vertices.empty() && !debug_world_indices.empty()) {
        create_mesh_buffers(
            debug_world_vertices,
            debug_world_indices,
            debug_world_vertex_buffer,
            debug_world_vertex_memory,
            debug_world_index_buffer,
            debug_world_index_memory);
    }

    if (!debug_screen_vertices.empty() && !debug_screen_indices.empty()) {
        create_mesh_buffers(
            debug_screen_vertices,
            debug_screen_indices,
            debug_screen_vertex_buffer,
            debug_screen_vertex_memory,
            debug_screen_index_buffer,
            debug_screen_index_memory);
    }
}

void VulkanRenderer::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin);

    std::array<VkClearValue, 2> clear{};
    clear[0].color = { {0.08f, 0.1f, 0.14f, 1.0f} };
    clear[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = render_pass;
    rp.framebuffer = framebuffers[image_index];
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = swapchain_extent;
    rp.clearValueCount = static_cast<uint32_t>(clear.size());
    rp.pClearValues = clear.data();

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    auto begin_label = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(device, "vkCmdBeginDebugUtilsLabelEXT"));
    auto end_label = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(device, "vkCmdEndDebugUtilsLabelEXT"));
    if (begin_label && end_label) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = "VOXOV.ScenePass";
        label.color[0] = 0.2f;
        label.color[1] = 0.7f;
        label.color[2] = 1.0f;
        label.color[3] = 1.0f;
        begin_label(cmd, &label);
    }

    for (uint32_t i = 0; i < current_view_count; ++i) {
        const glm::vec4 vp = current_viewports[i];
        const int vx = static_cast<int>(vp.x * static_cast<float>(swapchain_extent.width));
        const int vy = static_cast<int>(vp.y * static_cast<float>(swapchain_extent.height));
        const uint32_t vw = std::max(1u, static_cast<uint32_t>(vp.z * static_cast<float>(swapchain_extent.width)));
        const uint32_t vh = std::max(1u, static_cast<uint32_t>(vp.w * static_cast<float>(swapchain_extent.height)));

        VkViewport viewport{};
        viewport.x = static_cast<float>(vx);
        viewport.y = static_cast<float>(vy);
        viewport.width = static_cast<float>(vw);
        viewport.height = static_cast<float>(vh);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { vx, vy };
        scissor.extent = { vw, vh };

        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        PushConstants push{};
        push.view_proj = current_view_proj[i];
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);

        if (static_vertex_buffer != VK_NULL_HANDLE && static_index_buffer != VK_NULL_HANDLE && static_index_count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            const VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, &static_vertex_buffer, offsets);
            vkCmdBindIndexBuffer(cmd, static_index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, static_index_count, 1, 0, 0, 0);
        }

        if (debug_world_vertex_buffer != VK_NULL_HANDLE && debug_world_index_buffer != VK_NULL_HANDLE && debug_world_index_count > 0) {
            vkCmdBindPipeline(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                current_debug_xray ? pipeline_no_depth : pipeline);
            const VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, &debug_world_vertex_buffer, offsets);
            vkCmdBindIndexBuffer(cmd, debug_world_index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, debug_world_index_count, 1, 0, 0, 0);
        }

        if (debug_screen_vertex_buffer != VK_NULL_HANDLE && debug_screen_index_buffer != VK_NULL_HANDLE && debug_screen_index_count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_no_depth);
            const VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, &debug_screen_vertex_buffer, offsets);
            vkCmdBindIndexBuffer(cmd, debug_screen_index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, debug_screen_index_count, 1, 0, 0, 0);
        }
    }

    if (end_label) {
        end_label(cmd);
    }
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}
