#ifndef EMPER_BACKEND_SDL3_RENDERER
#define EMPER_BACKEND_SDL3_RENDERER

#include <emper/interfaces/backend/IRenderer.h>

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
    float frameDeltaSeconds() override;

    void beginFrame() override;
    void drawPoint(f32 x, f32 y, u32 color = 0x3399FFFF) override;
    void drawLine(f32 x1, f32 y1,
                  f32 x2, f32 y2, u32 color = 0x3399FFFF) override;
    void drawCircle(f32 x, f32 y, f32 radius,
                    u32 color = 0x3399FFFF) override;
    void endFrame() override;

private:
    SDL_Window* _window = nullptr;
    SDL_Renderer* _renderer = nullptr;
    std::uint64_t _lastTicks = 0;
};

} // namespace emper::backend

#endif // EMPER_BACKEND_SDL3_RENDERER