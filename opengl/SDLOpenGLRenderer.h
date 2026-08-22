#pragma once

#include <glad/gl.h>
#include <emper/interfaces/backend/IRenderer.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace emper::backend
{

class SDLOpenGLRenderer final
    : public emper::interfaces::backend::IRendererShaderPipeline, public interfaces::backend::IRenderer {
public:
    SDLOpenGLRenderer(const char* title, int width, int height);
    ~SDLOpenGLRenderer() override;

    SDLOpenGLRenderer(const SDLOpenGLRenderer&) = delete;
    SDLOpenGLRenderer& operator=(const SDLOpenGLRenderer&) = delete;

    bool isValid() const noexcept { return m_valid; }

    bool processEvents() override;
    f32 frameDeltaSeconds() override;
    void beginFrame() override;
    void drawPoint(f32 x, f32 y, u32 color = 0x3399FFFF) override;
    void drawLine(f32 x1, f32 y1, f32 x2, f32 y2,
                  u32 color = 0x3399FFFF) override;
    void drawCircle(f32 x, f32 y, f32 radius,
                    u32 color = 0x3399FFFF) override;

    void drawText(
    std::string_view text,
    f32 x,
    f32 y,
    f32 size,
    u32 color = 0xFFFFFFFF
    ) override;

    void drawRect(
        f32 x,
        f32 y,
        f32 width,
        f32 height,
        u32 color = 0xFFFFFFFF
    ) override;


    void endFrame() override;
    int windowWidth() const override { return m_width; }
    int windowHeight() const override { return m_height; }

    ProgramHandle createGraphicsProgram(
        const std::string& vertex,
        const std::string& fragment
    ) override;

    void destroyProgram(
        ProgramHandle program
    ) override;

    void bindProgram(
        ProgramHandle program
    ) override;

    void bindStorageBuffer(
        u32 binding,
        BufferHandle buffer
    ) override;

    void drawPoints(
        u32 count
    ) override;

private:
    void releasePlatformResources() noexcept;

    // Batched immediate-mode point rendering used by the generic
    // IRenderer::drawPoint API (the CPU flock path).
    void ensurePointResources();
    void flushPointBatch();

    static const char* pointVertexSource();
    static const char* pointFragmentSource();

    GLuint m_pointProgram = 0;
    GLuint m_pointVAO = 0;
    GLuint m_pointVBO = 0;
    std::vector<float> m_pointBatch;

    // Text rendering (bitmap-font via SDL debug-text atlas + textured quads).
    void ensureTextResources();

    static const char* textVertexSource();
    static const char* textFragmentSource();

    GLuint m_textProgram = 0;
    GLuint m_textVAO = 0;
    GLuint m_textVBO = 0;
    GLuint m_fontTexture = 0;

    GLuint m_rectProgram = 0;
    GLuint m_rectVAO = 0;
    GLuint m_rectVBO = 0;

    void ensureRectResources();

    static const char* rectVertexSource();
    static const char* rectFragmentSource();

    static std::string readFile(
        const std::string& path
    );

    GLuint compileShader(
        GLenum type,
        const std::string& source
    );

    GLuint m_vao = 0;
    ProgramHandle m_nextHandle = 0;
    std::unordered_map<ProgramHandle, GLuint> m_programs;
    SDL_Window* m_window = nullptr;
    void* m_context = nullptr;
    std::uint64_t m_lastCounter = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_ownsVideoSubsystem = false;
    bool m_glLoaded = false;
    bool m_valid = false;
};

}
