#include "Graphics.hpp"

#include <imgui.h>
#include "Error.hpp"
#include "Log.hpp"

#ifdef _DEBUG
#define IMGUI_VULKAN_DEBUG_REPORT
#endif

#ifdef IMGUI_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugReport(VkDebugReportFlagsEXT flags,
                                                   VkDebugReportObjectTypeEXT objectType,
                                                   uint64_t object,
                                                   size_t location,
                                                   int32_t messageCode,
                                                   const char* pLayerPrefix,
                                                   const char* pMessage,
                                                   void* pUserData) {
    (void) flags; (void) object; (void) location; (void) messageCode; (void) pUserData; (void) pLayerPrefix; // Unused Parameters
    Log::Message("[Vulkan] Debug report from ObjectType: %i Message: %s", objectType, pMessage);
    return VK_FALSE;
}
#endif

Graphics::Graphics(Application* app, const char **extensions, uint32_t extensionCount) : m_Application(app) {
    Init(extensions, extensionCount);
}

Graphics::~Graphics() {
    CleanupVulkanWindow();
    CleanupVulkan();
}

void Graphics::CreateFrameBuffer(VkSurfaceKHR surface, const int &width, const int &height) {
    ImGui_ImplVulkanH_Window* wd = &m_MainWindowData;
    SetupVulkanWindow(wd, surface, width, height);
}

void Graphics::InitImGui() {
    ImGui_ImplVulkanH_Window* wd = &m_MainWindowData;

    // Setup Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();

    // Setup Platform / Renderer backends.
    ImGui_ImplSDL2_InitForVulkan(m_Application->GetWindowHandler());
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = m_Instance;
    initInfo.PhysicalDevice = m_PhysicalDevice;
    initInfo.Device = m_Device;
    initInfo.QueueFamily = m_QueueFamily;
    initInfo.Queue = m_Queue;
    initInfo.PipelineCache = m_PipelineCache;
    initInfo.DescriptorPool = m_DescriptorPool;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = m_MinImageCount;
    initInfo.ImageCount = wd->ImageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = m_Allocator;
    initInfo.CheckVkResultFn = Graphics::CheckVkResult;
    ImGui_ImplVulkan_Init(&initInfo, wd->RenderPass);

    // Load font
    ImFont* font = io.Fonts->AddFontFromFileTTF("assets\\fonts\\Fantasque Sans Mono Nerd Font.ttf", 16.0f);
    IM_ASSERT(font != nullptr);

    // Upload font
    {
        VkResult err;
        VkCommandPool commandPool = wd->Frames[wd->FrameIndex].CommandPool;
        VkCommandBuffer commandBuffer = wd->Frames[wd->FrameIndex].CommandBuffer;

        err = vkResetCommandPool(m_Device, commandPool, 0);
        CheckVkResult(err);
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags != VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        CheckVkResult(err);

        ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);

        VkSubmitInfo endInfo = {};
        endInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        endInfo.commandBufferCount = 1;
        endInfo.pCommandBuffers = &commandBuffer;
        err = vkEndCommandBuffer(commandBuffer);
        CheckVkResult(err);
        err = vkQueueSubmit(m_Queue, 1, &endInfo, VK_NULL_HANDLE);
        CheckVkResult(err);

        err = vkDeviceWaitIdle(m_Device);
        CheckVkResult(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }
}

void Graphics::Cleanup() {
    VkResult err;
    err = vkDeviceWaitIdle(m_Device);
    CheckVkResult(err);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void Graphics::RebuildSwapChain(const int& width, const int& height) {
    ImGui_ImplVulkan_SetMinImageCount(m_MinImageCount);
    ImGui_ImplVulkanH_CreateOrResizeWindow(m_Instance, m_PhysicalDevice, m_Device, &m_MainWindowData, m_QueueFamily, m_Allocator, width, height, m_MinImageCount);
    m_MainWindowData.FrameIndex = 0;
}

void Graphics::Draw() {
    ImGui_ImplVulkanH_Window* wd = &m_MainWindowData;

    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (m_showDemoWidow) {
        ImGui::ShowDemoWindow();
    }

    // Rendering
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    const bool isMinimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);
    if (!isMinimized) {
        wd->ClearValue.color.float32[0] = clearColor.r * clearColor.a;
        wd->ClearValue.color.float32[1] = clearColor.g * clearColor.a;
        wd->ClearValue.color.float32[2] = clearColor.b * clearColor.a;
        wd->ClearValue.color.float32[3] = clearColor.a;
        FrameRender(drawData);
        FramePresent();
    }
}

void Graphics::Init(const char **extensions, uint32_t extensionCount) {
    VkResult err;
    Log::Message("Vulkan Extension Count: %d extensions supports.", extensionCount);

    // Create Vulkan Instance
    {
        VkInstanceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensions;

#ifdef IMGUI_VULKAN_DEBUG_REPORT
        Log::Message("IMGUI_VULKAN_DEBUG_REPORT is on.");
        // Enabling validation layers
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = layers;

        // Enabling debug report extension
        // (we need additional storage, so we duplicate the user array to add our new extension to it)
        const char** extensionsEXT = (const char**) malloc(sizeof(const char*) * (extensionCount + 1));
        memcpy(extensionsEXT, extensions, extensionCount * sizeof(const char*));
        extensionsEXT[extensionCount] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
        createInfo.enabledExtensionCount = extensionCount + 1;
        createInfo.ppEnabledLayerNames = extensionsEXT;

        // Create Vulkan Instance
        err = vkCreateInstance(&createInfo, m_Allocator, &m_Instance);
        CheckVkResult(err);
        free(extensionsEXT);

        // Get the function pointer (required for any extensions)
        auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT) vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(vkCreateDebugReportCallbackEXT != nullptr);

        // Setup the debug report callback
        VkDebugReportCallbackCreateInfoEXT debugReportCI = {};
        debugReportCI.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debugReportCI.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debugReportCI.pfnCallback = DebugReport;
        debugReportCI.pUserData = nullptr;
        err = vkCreateDebugReportCallbackEXT(m_Instance, &debugReportCI, m_Allocator, &m_DebugReport);
        CheckVkResult(err);
#else
        // create Vulkan Instance without any debug feature.
        err = vkCreateInstance(&createInfo, m_Allocator, &m_Instance);
        CheckVkResult(err);
        IM_UNUSED(m_DebugReport);
#endif
    }

    // Setup GPU
    {
        uint32_t gpuCount;
        err = vkEnumeratePhysicalDevices(m_Instance, &gpuCount, nullptr);
        CheckVkResult(err);
        IM_ASSERT(gpuCount > 0);

        auto* GPUs = (VkPhysicalDevice*) malloc(sizeof(VkPhysicalDevice) * gpuCount);
        err = vkEnumeratePhysicalDevices(m_Instance, &gpuCount, GPUs);
        CheckVkResult(err);

        // If a number > 1 of GPUs got reported, find discrete GPU if present, or use first one available.
        // This covers most common cases (multi-gpu / integrated + dedicated graphics)
        // Handing more complicated setup (multiple dedicated GPUs) is out of scope of this sample.
        int useGPU = 0;
        for (int i = 0; i < static_cast<int>(gpuCount); i++) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(GPUs[i], &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                useGPU = i;
                break;
            }
        }

        m_PhysicalDevice = GPUs[useGPU];
        free(GPUs);
    }

    // Select graphics queue family
    {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, nullptr);
        auto* queues = (VkQueueFamilyProperties*) malloc(sizeof(VkQueueFamilyProperties) * count);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, queues);
        for (uint32_t i = 0; i < count; i++) {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_QueueFamily = i;
                break;
            }
        }
        free(queues);
        IM_ASSERT(m_QueueFamily != (uint32_t)-1);
    }

    // Create Logical Device (with 1 queue)
    {
        int deviceExtensionCount = 1;
        const char* deviceExtensions[] = { "VK_KHR_swapchain" };
        const float queuePriority[] = { 1.0f };
        VkDeviceQueueCreateInfo queueInfo[1] = {};
        queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo[0].queueFamilyIndex = m_QueueFamily;
        queueInfo[0].queueCount = 1;
        queueInfo[0].pQueuePriorities = queuePriority;

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = sizeof(queueInfo) / sizeof(queueInfo[0]);
        createInfo.pQueueCreateInfos = queueInfo;
        createInfo.enabledExtensionCount = deviceExtensionCount;
        createInfo.ppEnabledExtensionNames = deviceExtensions;
        err = vkCreateDevice(m_PhysicalDevice, &createInfo, m_Allocator, &m_Device);
        CheckVkResult(err);
        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
    }

    // Create Descriptor Pool
    {
        VkDescriptorPoolSize poolSizes[] = {
                { VK_DESCRIPTOR_TYPE_SAMPLER,                   1000 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,    1000 },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,             1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,             1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,      1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,      1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,            1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,    1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,    1000 },
                { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,         1000 }
        };
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
        poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
        poolInfo.pPoolSizes = poolSizes;
        err = vkCreateDescriptorPool(m_Device, &poolInfo, m_Allocator, &m_DescriptorPool);
        CheckVkResult(err);
    }
}

// All the ImGui_ImplVulkanH_XXX structures / functions are optional helpers used by the demo.
// Your real engine / app may not use them.
void Graphics::SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, const int& width, const int& height) {
    wd->Surface = surface;

    // Check for WSI support
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, m_QueueFamily, wd->Surface, &res);
    if (res != VK_TRUE) {
        Log::Message("Error no WSI support on physical device 0");
        exit(Error::VKCreateFrameBufferFailed);
    }

    // Select Present Format
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(m_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t) IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

    // Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
    VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
    VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_PhysicalDevice, wd->Surface, &presentModes[0], IM_ARRAYSIZE(presentModes));

    // Create SwapChain, RenderPass, Framebuffer, etc.
    IM_ASSERT(m_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(m_Instance, m_PhysicalDevice, m_Device, wd, m_QueueFamily, m_Allocator, width, height, m_MinImageCount);
}

void Graphics::CleanupVulkan() {
    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, m_Allocator);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
    auto vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT) vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT");
    vkDestroyDebugReportCallbackEXT(m_Instance, m_DebugReport, m_Allocator);
#endif

    vkDestroyDevice(m_Device, m_Allocator);
    vkDestroyInstance(m_Instance, m_Allocator);
}

void Graphics::CleanupVulkanWindow() {
    ImGui_ImplVulkanH_DestroyWindow(m_Instance, m_Device, &m_MainWindowData, m_Allocator);
}

void Graphics::CheckVkResult(VkResult err) {
    if (err == 0) {
        return;
    }

    Log::Message("[Vulkan] Error: VkResult = %d", err);
    if (err < 0) {
        abort();
    }
}

void Graphics::FrameRender(ImDrawData *drawData) {
    ImGui_ImplVulkanH_Window* wd = &m_MainWindowData;

    VkResult err;
    VkSemaphore imageAcquiredSemaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore renderCompleteSemaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    err = vkAcquireNextImageKHR(m_Device, wd->Swapchain, UINT64_MAX, imageAcquiredSemaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        m_Application->SetSwapChainRebuild(true);
        return;
    }
    CheckVkResult(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(m_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        CheckVkResult(err);

        err = vkResetFences(m_Device, 1, &fd->Fence);
        CheckVkResult(err);
    }
    {
        err = vkResetCommandPool(m_Device, fd->CommandPool, 0);
        CheckVkResult(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        CheckVkResult(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Render dear imgui primitives into command buffer
    ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &imageAcquiredSemaphore;
        info.pWaitDstStageMask = &waitStage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &renderCompleteSemaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        CheckVkResult(err);
        err = vkQueueSubmit(m_Queue, 1, &info, fd->Fence);
        CheckVkResult(err);
    }
}
void Graphics::FramePresent() {
    if (m_Application->GetSwapChainRebuild()) {
        return;
    }

    ImGui_ImplVulkanH_Window* wd = &m_MainWindowData;
    VkSemaphore renderCompleteSemaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &renderCompleteSemaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(m_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        m_Application->SetSwapChainRebuild(true);
        return;
    }
    CheckVkResult(err);

    // Now we can use the next set of semaphores
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount;
}