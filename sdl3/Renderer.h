#ifndef EMPER_BACKEND_SDL3_RENDERER
#define EMPER_BACKEND_SDL3_RENDERER
#include <emper/interfaces/backend/IRenderer.h>

#include "Renderer.h"

#include <cstdint>

struct SDL_Window;
struct SDL_Renderer;

namespace emper::backend {

// SDL3-backed renderer.
//
// The constructor performs the full SDL lifecycle for a window + renderer
// (SDL_Init, SDL_CreateWindow, SDL_CreateRenderer), so engine consumers do
// not have to touch SDL directly: just construct one and hand it to the
// Simulation via Simulation::setRenderer().
class SDL3Renderer final : public interfaces::backend::IRenderer {
public:
    SDL3Renderer(const char* title, int width, int height);
    ~SDL3Renderer() override;

    SDL3Renderer(const SDL3Renderer&) = delete;
    SDL3Renderer& operator=(const SDL3Renderer&) = delete;

    // Pump the SDL event queue. Returns false when a quit event was seen.
    bool processEvents() override;

    int windowWidth() const;
    int windowHeight() const;

    // Seconds elapsed since the previous frameDeltaSeconds() call.
    float frameDeltaSeconds();

    void beginFrame() override;
    void drawPoint(float x, float y) override;
    void drawLine(float x1, float y1,
                  float x2, float y2) override;
    void drawCircle(float x, float y, float r) override;
    void endFrame() override;

private:
    SDL_Window* _window = nullptr;
    SDL_Renderer* _renderer = nullptr;
    std::uint64_t _lastTicks = 0;
};

}

#endif//EMPER_BACKEND_SDL3_RENDERER
