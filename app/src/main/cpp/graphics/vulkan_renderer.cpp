#include "vulkan_renderer.h"

#include <android/log.h>
#include <dlfcn.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#define LOG_TAG "Smash64Vulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace smash64::gfx {

namespace {

void ensureDir(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

}  // namespace

bool VulkanRenderer::init(SDL_Window* window, const std::string& shaderCacheDir, bool preferTurnip) {
    window_ = window;
    ensureDir(shaderCacheDir);
    cachePath_ = shaderCacheDir + "/pipeline.cache";

    if (!createInstance(preferTurnip)) return false;
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        LOGE("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return false;
    }
    if (!selectPhysicalDevice()) return false;
    if (!createDeviceAndQueue()) return false;
    loadPipelineCache();

    int w = 1280;
    int h = 720;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (!createSwapchain(w, h)) return false;
    if (!createSyncAndCommandPool()) return false;
    if (!initImGui(window_)) return false;

    initialized_ = true;
    LOGI("VulkanRenderer ready on GPU: %s (Turnip=%d)", gpuName_.c_str(), usingTurnip_ ? 1 : 0);
    return true;
}

void VulkanRenderer::shutdown() {
    if (!initialized_) return;
    vkDeviceWaitIdle(device_);

    savePipelineCache();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (imguiDescPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, imguiDescPool_, nullptr);
    if (pipelineCache_ != VK_NULL_HANDLE) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
    if (imageAvailableSem_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, imageAvailableSem_, nullptr);
    if (renderFinishedSem_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, renderFinishedSem_, nullptr);
    if (inFlightFence_ != VK_NULL_HANDLE) vkDestroyFence(device_, inFlightFence_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);

    destroySwapchain();
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    if (surface_ != VK_NULL_HANDLE) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);

    initialized_ = false;
}

bool VulkanRenderer::createInstance(bool preferTurnip) {
    if (preferTurnip) {
        // Attempt optional libadrenotools / custom Turnip ICD if user placed it in app files
        void* turnipHandle = dlopen("libvulkan_freedreno.so", RTLD_NOW | RTLD_LOCAL);
        if (turnipHandle) {
            usingTurnip_ = true;
            LOGI("Loaded custom Mesa Turnip Vulkan driver");
        } else {
            LOGI("Custom Turnip driver not found, falling back to system Vulkan");
        }
    }

    Uint32 extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    std::vector<const char*> extensions;
    for (Uint32 i = 0; i < extCount; ++i) {
        extensions.push_back(sdlExts[i]);
    }

    std::vector<const char*> layers;
#ifdef SMASH64_ENABLE_VK_VALIDATION
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> avail(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, avail.data());
    for (const auto& lp : avail) {
        if (std::strcmp(lp.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            LOGI("Enabled VK_LAYER_KHRONOS_validation");
            break;
        }
    }
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Smash64 Recomp (NALP)";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "N64ModernRuntime-Android";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    VkResult res = vkCreateInstance(&ci, nullptr, &instance_);
    if (res != VK_SUCCESS) {
        LOGE("vkCreateInstance failed: %d", res);
        return false;
    }
    return true;
}

bool VulkanRenderer::selectPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        LOGE("No Vulkan physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance_, &count, devs.data());

    physicalDevice_ = devs[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    gpuName_ = props.deviceName;

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qCount, qProps.data());

    for (uint32_t i = 0; i < qCount; ++i) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &presentSupport);
        if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
            graphicsQueueFamily_ = i;
            return true;
        }
    }
    LOGE("No graphics+present queue family found");
    return false;
}

bool VulkanRenderer::createDeviceAndQueue() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = graphicsQueueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;

    VkResult res = vkCreateDevice(physicalDevice_, &dci, nullptr, &device_);
    if (res != VK_SUCCESS) {
        LOGE("vkCreateDevice failed: %d", res);
        return false;
    }
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    return true;
}

bool VulkanRenderer::createSwapchain(int width, int height) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = f;
            break;
        }
    }
    swapchainFormat_ = chosen.format;

    if (caps.currentExtent.width != UINT32_MAX) {
        swapchainExtent_ = caps.currentExtent;
    } else {
        swapchainExtent_.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
        swapchainExtent_.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imgCount = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) {
        imgCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface_;
    sci.minImageCount = imgCount;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = swapchainExtent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS) {
        LOGE("vkCreateSwapchainKHR failed");
        return false;
    }

    if (renderPass_ == VK_NULL_HANDLE) {
        VkAttachmentDescription colorAtt{};
        colorAtt.format = swapchainFormat_;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &colorAtt;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;

        if (vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_) != VK_SUCCESS) {
            LOGE("vkCreateRenderPass failed");
            return false;
        }
    }

    uint32_t scImgCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &scImgCount, nullptr);
    swapchainImages_.resize(scImgCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &scImgCount, swapchainImages_.data());

    swapchainViews_.resize(scImgCount);
    framebuffers_.resize(scImgCount);
    for (uint32_t i = 0; i < scImgCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = swapchainImages_[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = swapchainFormat_;
        ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &ivci, nullptr, &swapchainViews_[i]);

        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = renderPass_;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &swapchainViews_[i];
        fbci.width = swapchainExtent_.width;
        fbci.height = swapchainExtent_.height;
        fbci.layers = 1;
        vkCreateFramebuffer(device_, &fbci, nullptr, &framebuffers_[i]);
    }

    return true;
}

void VulkanRenderer::destroySwapchain() {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto iv : swapchainViews_) vkDestroyImageView(device_, iv, nullptr);
    framebuffers_.clear();
    swapchainViews_.clear();
    swapchainImages_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool VulkanRenderer::createSyncAndCommandPool() {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = graphicsQueueFamily_;
    if (vkCreateCommandPool(device_, &cpci, nullptr, &commandPool_) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &cbai, &commandBuffer_) != VK_SUCCESS) return false;

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    vkCreateSemaphore(device_, &sci, nullptr, &imageAvailableSem_);
    vkCreateSemaphore(device_, &sci, nullptr, &renderFinishedSem_);
    vkCreateFence(device_, &fci, nullptr, &inFlightFence_);
    return true;
}

bool VulkanRenderer::initImGui(SDL_Window* window) {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
    };
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 64;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device_, &dpci, nullptr, &imguiDescPool_) != VK_SUCCESS) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().FontGlobalScale = 1.25f;

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance_;
    initInfo.PhysicalDevice = physicalDevice_;
    initInfo.Device = device_;
    initInfo.QueueFamily = graphicsQueueFamily_;
    initInfo.Queue = graphicsQueue_;
    initInfo.PipelineCache = pipelineCache_;
    initInfo.DescriptorPool = imguiDescPool_;
    initInfo.RenderPass = renderPass_;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = static_cast<uint32_t>(swapchainImages_.size());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);
    return true;
}

void VulkanRenderer::loadPipelineCache() {
    std::vector<uint8_t> data;
    FILE* f = std::fopen(cachePath_.c_str(), "rb");
    if (f) {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::rewind(f);
        if (sz > 0) {
            data.resize(static_cast<size_t>(sz));
            std::fread(data.data(), 1, data.size(), f);
        }
        std::fclose(f);
    }
    VkPipelineCacheCreateInfo pcci{};
    pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    pcci.initialDataSize = data.size();
    pcci.pInitialData = data.empty() ? nullptr : data.data();
    vkCreatePipelineCache(device_, &pcci, nullptr, &pipelineCache_);
}

void VulkanRenderer::savePipelineCache() {
    if (pipelineCache_ == VK_NULL_HANDLE) return;
    size_t sz = 0;
    vkGetPipelineCacheData(device_, pipelineCache_, &sz, nullptr);
    if (sz == 0) return;
    std::vector<uint8_t> data(sz);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &sz, data.data()) == VK_SUCCESS) {
        FILE* f = std::fopen(cachePath_.c_str(), "wb");
        if (f) {
            std::fwrite(data.data(), 1, sz, f);
            std::fclose(f);
        }
    }
}

bool VulkanRenderer::beginFrame(int& outWidth, int& outHeight) {
    vkWaitForFences(device_, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);
    VkResult res = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailableSem_,
                                         VK_NULL_HANDLE, &currentImageIndex_);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(device_);
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w > 0 && h > 0) {
            destroySwapchain();
            createSwapchain(w, h);
        }
        return false;
    }
    vkResetFences(device_, 1, &inFlightFence_);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    outWidth = static_cast<int>(swapchainExtent_.width);
    outHeight = static_cast<int>(swapchainExtent_.height);
    return true;
}

void VulkanRenderer::endFrame() {
    ImGui::Render();

    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr};
    vkBeginCommandBuffer(commandBuffer_, &cbbi);

    VkClearValue clearVal{};
    clearVal.color = {{0.05f, 0.06f, 0.09f, 1.0f}};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = renderPass_;
    rpbi.framebuffer = framebuffers_[currentImageIndex_];
    rpbi.renderArea = {{0, 0}, swapchainExtent_};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearVal;

    vkCmdBeginRenderPass(commandBuffer_, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer_);
    vkCmdEndRenderPass(commandBuffer_);
    vkEndCommandBuffer(commandBuffer_);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &imageAvailableSem_;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &commandBuffer_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinishedSem_;

    vkQueueSubmit(graphicsQueue_, 1, &si, inFlightFence_);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinishedSem_;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &currentImageIndex_;
    vkQueuePresentKHR(graphicsQueue_, &pi);
}

}  // namespace smash64::gfx
