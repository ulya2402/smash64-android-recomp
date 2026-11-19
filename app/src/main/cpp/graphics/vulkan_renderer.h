#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace smash64::gfx {

class VulkanRenderer {
public:
    bool init(SDL_Window* window, const std::string& shaderCacheDir, bool preferTurnip);
    void shutdown();

    bool beginFrame(int& outWidth, int& outHeight);
    void endFrame();

    const char* activeGpuName() const { return gpuName_.c_str(); }
    bool isUsingCustomTurnip() const { return usingTurnip_; }

private:
    bool createInstance(bool preferTurnip);
    bool selectPhysicalDevice();
    bool createDeviceAndQueue();
    bool createSwapchain(int width, int height);
    void destroySwapchain();
    bool createSyncAndCommandPool();
    bool initImGui(SDL_Window* window);
    void loadPipelineCache();
    void savePipelineCache();

    SDL_Window* window_ = nullptr;
    std::string cachePath_;
    std::string gpuName_ = "Unknown";
    bool usingTurnip_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkDescriptorPool imguiDescPool_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;

    VkSemaphore imageAvailableSem_ = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSem_ = VK_NULL_HANDLE;
    VkFence inFlightFence_ = VK_NULL_HANDLE;
    uint32_t currentImageIndex_ = 0;
    bool initialized_ = false;
};

}  // namespace smash64::gfx
