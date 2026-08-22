#pragma once

#include <glad/gl.h>
#include <emper/interfaces/backend/IRenderer.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace emper::backend
{

class SDLOpenGLRenderer final
    : public emper::interfaces::backend::IRendererShaderPipeline,
      public interfaces::backend::IRenderer {
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

    // IRendererShaderPipeline (compute-backed rendering path).
    // This API remains immediate-mode and untouched by batching.
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

    // ------------------------------------------------------------------
    // CPU-side batch records.
    //
    // draw*() only appends records to these vectors. endFrame() uploads
    // each batch once and submits it with one (or a few) draw call(s).
    // ------------------------------------------------------------------

    struct BatchPoint
    {
        f32 x;                  // NDC position (existing point convention:
                                //   x * 2/width  - 1,
                                //   y * 2/height - 1)
        f32 y;
        f32 r, g, b, a;         // normalized color 0xRRGGBBAA
    };

    struct BatchCircle
    {
        f32 x;                  // pixel position, (0,0) = top-left
        f32 y;
        f32 radius;             // pixels
        f32 padding;            // unused, keeps two vec4 instances
        f32 r, g, b, a;         // normalized color
    };

    struct BatchRect
    {
        f32 x;                  // pixel position, (0,0) = top-left
        f32 y;
        f32 width;              // pixels
        f32 height;
        f32 r, g, b, a;         // normalized color
    };

    struct BatchTextVertex
    {
        f32 x;                  // NDC position
        f32 y;
        f32 u, v;               // glyph atlas UV
        f32 r, g, b, a;         // normalized color (per-string batch color)
    };

    std::vector<BatchPoint> m_pointBatch;
    std::vector<BatchPoint> m_lineBatch;       // two entries per line
    std::vector<BatchCircle> m_circleBatch;
    std::vector<BatchRect> m_rectBatch;
    std::vector<BatchTextVertex> m_textBatch;

    // ------------------------------------------------------------------
    // Points: interleaved vertex buffer, one glDrawArrays(GL_POINTS).
    // ------------------------------------------------------------------
    void ensurePointResources();
    void flushPointBatch();

    static const char* pointVertexSource();
    static const char* pointFragmentSource();

    GLuint m_pointProgram = 0;
    GLuint m_pointVAO = 0;
    GLuint m_pointVBO = 0;

    // ------------------------------------------------------------------
    // Lines: interleaved vertex buffer, one glDrawArrays(GL_LINES).
    // ------------------------------------------------------------------
    void ensureLineResources();
    void flushLineBatch();

    static const char* lineVertexSource();
    static const char* lineFragmentSource();

    GLuint m_lineProgram = 0;
    GLuint m_lineVAO = 0;
    GLuint m_lineVBO = 0;

    // ------------------------------------------------------------------
    // Circles: shared unit-circle mesh + per-instance data.
    // One glDrawElementsInstanced for the whole batch.
    // ------------------------------------------------------------------
    void ensureCircleResources();
    void flushCircleBatch();

    static const char* circleVertexSource();
    static const char* circleFragmentSource();

    GLuint m_circleProgram = 0;
    GLuint m_circleVAO = 0;
    GLuint m_circleVBO = 0;            // shared unit-circle vertex mesh
    GLuint m_circleEBO = 0;            // shared unit-circle index mesh
    GLuint m_circleInstanceVBO = 0;    // per-frame instance data
    GLint m_circleViewScaleLoc = -1;

    // ------------------------------------------------------------------
    // Rectangles: shared unit quad + per-instance data.
    // One glDrawElementsInstanced for the whole batch.
    // ------------------------------------------------------------------
    void ensureRectResources();
    void flushRectBatch();

    static const char* rectVertexSource();
    static const char* rectFragmentSource();

    GLuint m_rectProgram = 0;
    GLuint m_rectVAO = 0;
    GLuint m_rectVBO = 0;            // shared unit quad
    GLuint m_rectEBO = 0;            // shared unit quad indices
    GLuint m_rectInstanceVBO = 0;    // per-frame instance data
    GLint m_rectViewportLoc = -1;

    // ------------------------------------------------------------------
    // Text: SDL debug-font atlas + batched quad vertices.
    // One glDrawArrays(GL_TRIANGLES) for the whole batch.
    // ------------------------------------------------------------------
    void ensureTextResources();
    void flushTextBatch();

    static const char* textVertexSource();
    static const char* textFragmentSource();

    GLuint m_textProgram = 0;
    GLuint m_textVAO = 0;
    GLuint m_textVBO = 0;
    GLuint m_fontTexture = 0;

    // ------------------------------------------------------------------
    // Shared helpers.
    // ------------------------------------------------------------------
    static std::string readFile(
        const std::string& path
    );

    GLuint compileShader(
        GLenum type,
        const std::string& source
    );

    void enableBlending() noexcept;

    // ------------------------------------------------------------------
    // Shader pipeline (compute-backed rendering) state.
    // ------------------------------------------------------------------
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

} // namespace emper::backend