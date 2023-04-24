#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include "Application.h"

class Application;

class Graphics {
public:
    Graphics(Application* app, const char** extensions, uint32_t extensionCount);
    ~Graphics();

    const VkInstance& GetInstance() const { return m_Instance; }
    ImGui_ImplVulkanH_Window* GetMainWindowData() { return &m_MainWindowData; }

    void CreateFrameBuffer(VkSurfaceKHR surface, const int& width, const int& height);
    void InitImGui();
    void Cleanup();

    void RebuildSwapChain(const int& width, const int& height);
    void Draw();

    static void CheckVkResult(VkResult err);

private:
    void Init(const char** extensions, uint32_t extensionCount);
    void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, const int& width, const int& height);
    void CleanupVulkanWindow();
    void CleanupVulkan();

    void FrameRender(ImDrawData* drawData);
    void FramePresent();

    Application*              m_Application;
    VkAllocationCallbacks*    m_Allocator = nullptr;
    VkInstance                m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice          m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice                  m_Device = VK_NULL_HANDLE;
    uint32_t                  m_QueueFamily = (uint32_t)-1;
    VkQueue                   m_Queue = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT  m_DebugReport = VK_NULL_HANDLE;
    VkPipelineCache           m_PipelineCache = VK_NULL_HANDLE;
    VkDescriptorPool          m_DescriptorPool = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window  m_MainWindowData {};

    uint32_t m_MinImageCount = 2;

    bool m_showDemoWidow = true;
    glm::vec4 clearColor = { 0.45f, 0.55f, 0.60f, 1.0f };

};

#endif
