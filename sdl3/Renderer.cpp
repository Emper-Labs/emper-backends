#include "Renderer.h"

#include <SDL3/SDL.h>

namespace {

void setDrawColor(SDL_Renderer* renderer, emper::u32 color)
{
    const auto red = static_cast<emper::u8>((color >> 24) & 0xFFu);
    const auto green = static_cast<emper::u8>((color >> 16) & 0xFFu);
    const auto blue = static_cast<emper::u8>((color >> 8) & 0xFFu);
    const auto alpha = static_cast<emper::u8>(color & 0xFFu);

    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
}

} // namespace

namespace emper::backend {

SDL3Renderer::SDL3Renderer(const char* title, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    _window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (!_window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return;
    }

    // Disable VSync so the main loop is not throttled to the display
    // refresh rate (important for benchmarking / high-frame-rate apps).
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

    _renderer = SDL_CreateRenderer(_window, nullptr);
    if (!_renderer)
    {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(_window);
        SDL_Quit();
        return;
    }

    _lastTicks = SDL_GetTicks();
}

SDL3Renderer::~SDL3Renderer()
{
    if (_renderer)
    {
        SDL_DestroyRenderer(_renderer);
    }
    if (_window)
    {
        SDL_DestroyWindow(_window);
    }
    SDL_Quit();
}

bool SDL3Renderer::processEvents()
{
    if (!_window)
    {
        return false;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            return false;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            return false;
        }
    }
    return true;
}

int SDL3Renderer::windowWidth() const
{
    int w = 0;
    if (_window)
    {
        SDL_GetWindowSize(_window, &w, nullptr);
    }
    return w;
}

int SDL3Renderer::windowHeight() const
{
    int h = 0;
    if (_window)
    {
        SDL_GetWindowSize(_window, nullptr, &h);
    }
    return h;
}

float SDL3Renderer::frameDeltaSeconds()
{
    const std::uint64_t now = SDL_GetTicks();
    const std::uint64_t elapsed = now - _lastTicks;
    _lastTicks = now;
    return static_cast<float>(elapsed) / 1000.0f;
}

void SDL3Renderer::beginFrame()
{
    if (!_renderer)
    {
        return;
    }
    SDL_SetRenderDrawColor(_renderer, 15, 15, 20, 255);
    SDL_RenderClear(_renderer);
}

void SDL3Renderer::drawPoint(float x, float y, u32 color)
{
    if (!_renderer)
    {
        return;
    }

    setDrawColor(_renderer, color);
    SDL_RenderPoint(_renderer, x, y);
}

void SDL3Renderer::drawLine(float x1, float y1,
                            float x2, float y2, u32 color)
{
    if (!_renderer)
    {
        return;
    }

    setDrawColor(_renderer, color);
    SDL_RenderLine(_renderer, x1, y1, x2, y2);
}

void SDL3Renderer::drawCircle(float x, float y, float radius, u32 color)
{
    // SDL3 (3.4.x) does not provide a native circle primitive, so we
    // approximate the circle with a closed polygon of line segments.
    if (!_renderer || radius <= 0.0f)
    {
        return;
    }

    setDrawColor(_renderer, color);

    constexpr int Segments = 32;
    constexpr float TwoPi = 6.28318530717958647692f;

    float prevX = x + radius;
    float prevY = y;

    for (int i = 1; i <= Segments; ++i)
    {
        const float angle = TwoPi * static_cast<float>(i) / static_cast<float>(Segments);
        const float currX = x + radius * SDL_cosf(angle);
        const float currY = y + radius * SDL_sinf(angle);

        SDL_RenderLine(_renderer, prevX, prevY, currX, currY);

        prevX = currX;
        prevY = currY;
    }
}

void SDL3Renderer::endFrame()
{
    if (!_renderer)
    {
        return;
    }
    SDL_RenderPresent(_renderer);
}

} // namespace emper::backend