#include "vulkan_app.hpp"
#include <iostream>
#include <array>
#include <cstring>
#include <algorithm>
#include <limits>

const int MAX_FRAMES_IN_FLIGHT = 2;

// Simple vertex data
const std::vector<Vertex> vertices = {
    // Triangle
    Vertex{{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    Vertex{{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    Vertex{{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {0, 1, 2};

// Vertex input descriptions
VkVertexInputBindingDescription Vertex::getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 2> Vertex::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);
    
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);
    
    return attributeDescriptions;
}

VulkanApp::VulkanApp() {}
VulkanApp::~VulkanApp() { cleanup(); }

void VulkanApp::init(SDL_Window* sdl_window) {
    window = sdl_window;
    
    // Minimal initialization for now
    create_instance();
    create_surface(window);
    pick_physical_device();
    create_logical_device();
    
    std::cout << "Vulkan basic initialization completed" << std::endl;
}

void VulkanApp::cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
    }
    
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

void VulkanApp::draw_frame() {
    // Simple stub - no actual rendering yet
}

void VulkanApp::handle_resize(int width, int height) {
    std::cout << "Window resized to " << width << "x" << height << std::endl;
}

void VulkanApp::create_instance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Voxov";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    uint32_t extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;
    
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
}

void VulkanApp::setup_debug_messenger() {}

void VulkanApp::create_surface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void VulkanApp::pick_physical_device() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    
    physical_device = devices[0];  // Just pick the first device
    
    if (physical_device == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

bool VulkanApp::is_device_suitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = find_queue_families(device);
    return indices.is_complete();
}

VulkanApp::QueueFamilyIndices VulkanApp::find_queue_families(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
        }
        
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        
        if (presentSupport) {
            indices.present_family = i;
        }
        
        if (indices.is_complete()) {
            break;
        }
        
        i++;
    }
    
    return indices;
}

void VulkanApp::create_logical_device() {
    QueueFamilyIndices indices = find_queue_families(physical_device);
    
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphics_family.value();
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    
    VkPhysicalDeviceFeatures deviceFeatures{};
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    
    if (vkCreateDevice(physical_device, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }
    
    vkGetDeviceQueue(device, indices.graphics_family.value(), 0, &graphics_queue);
    vkGetDeviceQueue(device, indices.present_family.value(), 0, &present_queue);
}

std::vector<char> VulkanApp::read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }
    
    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    
    file.close();
    
    return buffer;
}

VkShaderModule VulkanApp::create_shader_module(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    
    return shaderModule;
}

void VulkanApp::update_uniform_buffer(uint32_t currentImage) {
    // Stub for now
}

uint32_t VulkanApp::find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("failed to find suitable memory type!");
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanApp::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

void VulkanApp::populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {}

// Stub implementations for remaining required methods
void VulkanApp::create_swap_chain() { std::cout << "create_swap_chain stub" << std::endl; }
void VulkanApp::create_image_views() { std::cout << "create_image_views stub" << std::endl; }
void VulkanApp::create_render_pass() { std::cout << "create_render_pass stub" << std::endl; }
void VulkanApp::create_descriptor_set_layout() { std::cout << "create_descriptor_set_layout stub" << std::endl; }
void VulkanApp::create_graphics_pipeline() { std::cout << "create_graphics_pipeline stub" << std::endl; }
void VulkanApp::create_frame_buffers() { std::cout << "create_frame_buffers stub" << std::endl; }
void VulkanApp::create_command_pool() { std::cout << "create_command_pool stub" << std::endl; }
void VulkanApp::create_vertex_buffer() { std::cout << "create_vertex_buffer stub" << std::endl; }
void VulkanApp::create_index_buffer() { std::cout << "create_index_buffer stub" << std::endl; }
void VulkanApp::create_uniform_buffers() { std::cout << "create_uniform_buffers stub" << std::endl; }
void VulkanApp::create_descriptor_pool() { std::cout << "create_descriptor_pool stub" << std::endl; }
void VulkanApp::create_descriptor_sets() { std::cout << "create_descriptor_sets stub" << std::endl; }
void VulkanApp::create_command_buffers() { std::cout << "create_command_buffers stub" << std::endl; }
void VulkanApp::create_sync_objects() { std::cout << "create_sync_objects stub" << std::endl; }
void VulkanApp::cleanup_swap_chain() { std::cout << "cleanup_swap_chain stub" << std::endl; }
void VulkanApp::recreate_swap_chain() { std::cout << "recreate_swap_chain stub" << std::endl; }
void VulkanApp::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {}
void VulkanApp::copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {}
void VulkanApp::record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {}
SwapChainSupportDetails VulkanApp::query_swap_chain_support(VkPhysicalDevice device) { return {}; }
VkSurfaceFormatKHR VulkanApp::choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats) { return available_formats[0]; }
VkPresentModeKHR VulkanApp::choose_swap_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes) { return available_present_modes[0]; }
VkExtent2D VulkanApp::choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) { return capabilities.currentExtent; }