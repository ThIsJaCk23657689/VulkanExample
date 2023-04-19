#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <glm/glm.hpp>

#include <iostream>
#include <cstdarg>
#include <cstddef>

#ifdef _DEBUG
#define IMGUI_VULKAN_DEBUG_REPORT
#endif

// Data
static VkAllocationCallbacks*   g_Allocator = nullptr;
static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData;
static uint32_t                 g_MinImageCount = 2;
static bool                     g_SwapChainRebuild = false;
static constexpr size_t         g_MessageBufferSize = 4096;

void Message(const char* fmt...) {
    char buffer[g_MessageBufferSize];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, std::size(buffer), fmt, args);
    va_end(args);

    std::cout << "[Message] " << buffer << std::endl;
}

void CheckVkResult(VkResult err) {
    if (err == 0) {
        return;
    }

    Message("[Vulkan] Error: VkResult = %d", err);
    if (err < 0) {
        abort();
    }
}

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
    Message("[Vulkan] Debug report from ObjectType: %i Message: %s", objectType, pMessage);
    return VK_FALSE;
}
#endif

void SetupVulkan(const char** extensions, uint32_t extensionCount) {
    Message("Vulkan Extension Count: %d extensions supports.", extensionCount);

    VkResult err;

    // Create Vulkan Instance
    {
        VkInstanceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensions;

#ifdef IMGUI_VULKAN_DEBUG_REPORT
        // Enabling validation layers
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = layers;

        // Enabling debug report extension
        // (we need additional storage, so we duplicate the user array to add our new extension to it)
        const char** extensionsEXT = (const char**) malloc(sizeof(const char*) * (extensionCount + 1));
        memcpy(extensionsEXT, extensions, extensionCount * sizeof(const char*));
        extensionsEXT[extensionCount] = "VK_EXT_debug_report";
        createInfo.enabledExtensionCount = extensionCount + 1;
        createInfo.ppEnabledLayerNames = extensionsEXT;

        // Create Vulkan Instance
        err = vkCreateInstance(&createInfo, g_Allocator, &g_Instance);
        CheckVkResult(err);
        free(extensionsEXT);

        // Get the function pointer (required for any extensions)
        auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT) vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(vkCreateDebugReportCallbackEXT != nullptr);

        // Setup the debug report callback
        VkDebugReportCallbackCreateInfoEXT debugReportCI = {};
        debugReportCI.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debugReportCI.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debugReportCI.pfnCallback = DebugReport;
        debugReportCI.pUserData = nullptr;
        err = vkCreateDebugReportCallbackEXT(g_Instance, &debugReportCI, g_Allocator, &g_DebugReport);
        CheckVkResult(err);
#else
        // create Vulkan Instance without any debug feature.
        err = vkCreateInstance(&createInfo, g_Allocator, &g_Instance);
        CheckVkResult(err);
        IM_UNUSED(g_DebugReport);
#endif
    }

    // Setup GPU
    {
        uint32_t gpuCount;
        err = vkEnumeratePhysicalDevices(g_Instance, &gpuCount, nullptr);
        CheckVkResult(err);
        IM_ASSERT(gpuCount > 0);

        auto* GPUs = (VkPhysicalDevice*) malloc(sizeof(VkPhysicalDevice) * gpuCount);
        err = vkEnumeratePhysicalDevices(g_Instance, &gpuCount, GPUs);
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

        g_PhysicalDevice = GPUs[useGPU];
        free(GPUs);
    }

    // Select graphics queue family
    {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, nullptr);
        auto* queues = (VkQueueFamilyProperties*) malloc(sizeof(VkQueueFamilyProperties) * count);
        vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, queues);
        for (uint32_t i = 0; i < count; i++) {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                g_QueueFamily = i;
                break;
            }
        }
        free(queues);
        IM_ASSERT(g_QueueFamily != (uint32_t)-1);
    }

    // Create Logical Device (with 1 queue)
    {
        int deviceExtensionCount = 1;
        const char* deviceExtensions[] = { "VK_KHR_swapchain" };
        const float queuePriority[] = { 1.0f };
        VkDeviceQueueCreateInfo queueInfo[1] = {};
        queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo[0].queueFamilyIndex = g_QueueFamily;
        queueInfo[0].queueCount = 1;
        queueInfo[0].pQueuePriorities = queuePriority;

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = sizeof(queueInfo) / sizeof(queueInfo[0]);
        createInfo.pQueueCreateInfos = queueInfo;
        createInfo.enabledExtensionCount = deviceExtensionCount;
        createInfo.ppEnabledExtensionNames = deviceExtensions;
        err = vkCreateDevice(g_PhysicalDevice, &createInfo, g_Allocator, &g_Device);
        CheckVkResult(err);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
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
        err = vkCreateDescriptorPool(g_Device, &poolInfo, g_Allocator, &g_DescriptorPool);
        CheckVkResult(err);
    }
}

// All the ImGui_ImplVulkanH_XXX structures / functions are optional helpers used by the demo.
// Your real engine / app may not use them.
void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height) {
    wd->Surface = surface;

    // Check for WSI support
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
    if (res != VK_TRUE) {
        Message("Error no WSI support on physical device 0");
        exit(-1);
    }

    // Select Present Format
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t) IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

    // Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
    VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
    VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &presentModes[0], IM_ARRAYSIZE(presentModes));

    // Create SwapChain, RenderPass, Framebuffer, etc.
    IM_ASSERT(g_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
}

void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* drawData) {
    VkResult err;
    VkSemaphore imageAcquiredSemaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore renderCompleteSemaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, imageAcquiredSemaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
        return;
    }
    CheckVkResult(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        CheckVkResult(err);

        err = vkResetFences(g_Device, 1, &fd->Fence);
        CheckVkResult(err);
    }
    {
        err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
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
        err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
        CheckVkResult(err);
    }
}

void FramePresent(ImGui_ImplVulkanH_Window* wd) {
    if (g_SwapChainRebuild) {
        return;
    }

    VkSemaphore renderCompleteSemaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &renderCompleteSemaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
        return;
    }
    CheckVkResult(err);

    // Now we can use the next set of semaphores
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount;
}

void CleanupVulkanWindow() {
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}

void CleanupVulkan() {
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
    auto vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT) vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
    vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif

    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

int main(int argc, char** argv) {

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        Message("Oops! Failed to initialize SDL2, Error: %s", SDL_GetError());
        return -1;
    }
    Message("Initialize SDL2 successfully.");

#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Create Window
    const auto windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    auto window = SDL_CreateWindow("Hello Vulkan", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
    if (!window) {
        Message("Failed to create SDL2 window.");
        return -1;
    }
    Message("Create a SDL2 window successfully.");
    SDL_SetWindowMinimumSize(window, 400, 300);

    // Create window with Vulkan graphics context
    uint32_t extensionCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr);
    // vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    const char** extensions = new const char*[extensionCount];
    SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions);
    SetupVulkan(extensions, extensionCount);
    delete[] extensions;

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err;
    if (SDL_Vulkan_CreateSurface(window, g_Instance, &surface) == 0) {
        Message("Failed to create Vulkan surface.");
        return 1;
    }
    Message("Create a Vulkan surface successfully.");

    // Create Framebuffers
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    SetupVulkanWindow(wd, surface, w, h);

    // Setup Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();

    // Setup Platform / Renderer backends.
    ImGui_ImplSDL2_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = g_Instance;
    initInfo.PhysicalDevice = g_PhysicalDevice;
    initInfo.Device = g_Device;
    initInfo.QueueFamily = g_QueueFamily;
    initInfo.Queue = g_Queue;
    initInfo.PipelineCache = g_PipelineCache;
    initInfo.DescriptorPool = g_DescriptorPool;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = g_MinImageCount;
    initInfo.ImageCount = wd->ImageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = g_Allocator;
    initInfo.CheckVkResultFn = CheckVkResult;
    ImGui_ImplVulkan_Init(&initInfo, wd->RenderPass);

    // Load font
    ImFont* font = io.Fonts->AddFontFromFileTTF("assets\\fonts\\Fantasque Sans Mono Nerd Font.ttf", 16.0f);
    IM_ASSERT(font != nullptr);

    // Upload font
    {
        VkCommandPool commandPool = wd->Frames[wd->FrameIndex].CommandPool;
        VkCommandBuffer commandBuffer = wd->Frames[wd->FrameIndex].CommandBuffer;

        err = vkResetCommandPool(g_Device, commandPool, 0);
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
        err = vkQueueSubmit(g_Queue, 1, &endInfo, VK_NULL_HANDLE);
        CheckVkResult(err);

        err = vkDeviceWaitIdle(g_Device);
        CheckVkResult(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    // Our state
    bool shouldClose = false;
    bool showDemoWidow = true;
    glm::vec4 clearColor = { 0.45f, 0.55f, 0.60f, 1.0f };

    while (!shouldClose) {

        // Event Handling
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
                case SDL_QUIT:
                    shouldClose = true;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)) {
                        shouldClose = true;
                    }
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        shouldClose = true;
                    }
                    break;
            }
        }


        // Resize Swap Chain
        if (g_SwapChainRebuild) {
            int width, height;
            SDL_GetWindowSize(window, &width, &height);
            if (width > 0 && height > 0) {
                ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
                ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
                g_MainWindowData.FrameIndex = 0;
                g_SwapChainRebuild = false;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if (showDemoWidow) {
            ImGui::ShowDemoWindow();
        }

        // Rendering
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        const bool isMinmized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);
        if (!isMinmized) {
            wd->ClearValue.color.float32[0] = clearColor.r * clearColor.a;
            wd->ClearValue.color.float32[1] = clearColor.g * clearColor.a;
            wd->ClearValue.color.float32[2] = clearColor.b * clearColor.a;
            wd->ClearValue.color.float32[3] = clearColor.a;
            FrameRender(wd, drawData);
            FramePresent(wd);
        }
    }

    // Cleanup
    err = vkDeviceWaitIdle(g_Device);
    CheckVkResult(err);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkanWindow();
    CleanupVulkan();

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}