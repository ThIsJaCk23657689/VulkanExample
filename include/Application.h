#ifndef APPLICATION_H
#define APPLICATION_H

#include <SDL.h>
#include <string>
#include <memory>
#include "Graphics.hpp"

class Graphics;

struct Window {
    unsigned int width = 1280;
    unsigned int height = 720;
    std::string title = "Hello World";
    SDL_Window* handler = nullptr;
};

class Application {
public:
    Application();
    Application(const std::string& title, const unsigned int& width, const unsigned int& height);
    ~Application();

    void Run();
    SDL_Window* GetWindowHandler() const { return m_window.handler; }
    bool GetSwapChainRebuild() const { return m_SwapChainRebuild; }

    void SetSwapChainRebuild(const bool& enable) { m_SwapChainRebuild = enable; }

private:
    void InitSDLWindow();
    void InitVulkan();

    Window m_window;
    std::shared_ptr<Graphics> m_graphics = nullptr;

    bool m_shouldClose = false;
    bool m_SwapChainRebuild = false;

};

#endif
