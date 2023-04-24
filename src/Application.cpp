#include "Application.h"

#include <SDL_vulkan.h>

#include "Error.hpp"
#include "Log.hpp"

Application::Application() {
    InitSDLWindow();
    InitVulkan();
}

Application::Application(const std::string &title, const unsigned int &width, const unsigned int &height)
    : m_window({ width, height, title, nullptr }){
    InitSDLWindow();
    InitVulkan();
}

Application::~Application() {
    SDL_DestroyWindow(m_window.handler);
    SDL_Quit();
}

void Application::InitSDLWindow() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        Log::Message("Oops! Failed to initialize SDL2, Error: %s", SDL_GetError());
        exit(Error::SDLInitFailed);
    }
    Log::Message("Initialize SDL2 successfully.");

#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Create Window
    const auto windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    m_window.handler = SDL_CreateWindow(m_window.title.c_str(),
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        m_window.width,
                                        m_window.height,
                                        windowFlags);
    if (!m_window.handler) {
        Log::Message("Failed to create SDL2 window.");
        exit(Error::SDLWindowInitFailed);
    }
    Log::Message("Create a SDL2 window successfully.");
    SDL_SetWindowMinimumSize(m_window.handler, 400, 300);
}

void Application::InitVulkan() {
    // Create window with Vulkan graphics context
    uint32_t extensionCount = 0;
    SDL_Vulkan_GetInstanceExtensions(m_window.handler, &extensionCount, nullptr);

    const char** extensions = new const char*[extensionCount];
    SDL_Vulkan_GetInstanceExtensions(m_window.handler, &extensionCount, extensions);
    m_graphics = std::make_shared<Graphics>(this, extensions, extensionCount);
    delete[] extensions;

    // Create Window Surface
    VkSurfaceKHR surface;
    if (SDL_Vulkan_CreateSurface(m_window.handler, m_graphics->GetInstance(), &surface) == 0) {
        Log::Message("Failed to create Vulkan surface.");
        exit(Error::SDLVKSurfaceCreatedFailed);
    }
    Log::Message("Create a Vulkan surface successfully.");

    // Create Frame buffers
    int w, h;
    SDL_GetWindowSize(m_window.handler, &w, &h);
    m_graphics->CreateFrameBuffer(surface, w, h);

    // Init ImGui
    m_graphics->InitImGui();
}

void Application::Run() {
    while (!m_shouldClose) {

        // Event Handling
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
                case SDL_QUIT:
                    m_shouldClose = true;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(m_window.handler)) {
                        m_shouldClose = true;
                    }
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        m_shouldClose = true;
                    }
                    break;
            }
        }

        // Resize Swap Chain
        if (m_SwapChainRebuild) {
            int width, height;
            SDL_GetWindowSize(m_window.handler, &width, &height);
            if (width > 0 && height > 0) {
                m_graphics->RebuildSwapChain(width, height);
                m_SwapChainRebuild = false;
            }
        }

        m_graphics->Draw();
    }

    // Cleanup
    m_graphics->Cleanup();
}