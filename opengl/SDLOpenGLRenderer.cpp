#include "SDLOpenGLRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace
{

// Compile + link helper used by every ensure*Resources() path.
// Takes ownership of both compiled shaders regardless of outcome.
GLuint linkShaderProgram(GLuint vs, GLuint fs)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

// Common interleaved layout shared by points and lines:
//   floats 0-1: position
//   floats 2-5: color (RGBA)
void setupPosColorAttributes(GLuint vao, GLuint vbo)
{
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    constexpr GLsizei stride = 6 * sizeof(GLfloat);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        stride, reinterpret_cast<const void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE,
        stride, reinterpret_cast<const void*>(2 * sizeof(GLfloat)));

    glBindVertexArray(0);
}

} // namespace

namespace emper::backend
{

SDLOpenGLRenderer::SDLOpenGLRenderer(const char* title, int width, int height)
{
    const bool videoAlreadyInitialized =
        (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0;

    if (!videoAlreadyInitialized && !SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        SDL_Log(
            "SDL video initialization failed: %s",
            SDL_GetError()
        );

        return;
    }

    m_ownsVideoSubsystem = !videoAlreadyInitialized;

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MAJOR_VERSION,
        4
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MINOR_VERSION,
        3
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE
    );

    SDL_GL_SetAttribute(
        SDL_GL_DOUBLEBUFFER,
        1
    );

    m_window = SDL_CreateWindow(
        title,
        width,
        height,
        SDL_WINDOW_OPENGL |
        SDL_WINDOW_RESIZABLE
    );

    if (!m_window)
    {
        SDL_Log(
            "SDL_CreateWindow failed: %s",
            SDL_GetError()
        );

        releasePlatformResources();
        return;
    }


    SDL_GLContext context = SDL_GL_CreateContext(m_window);
    m_context = context;

    if (!m_context)
    {
        SDL_Log(
            "SDL_GL_CreateContext failed: %s",
            SDL_GetError()
        );

        releasePlatformResources();
        return;
    }

    if (!SDL_GL_MakeCurrent(
        m_window,
        context))
    {
        SDL_Log(
            "SDL_GL_MakeCurrent failed: %s",
            SDL_GetError()
        );

        releasePlatformResources();
        return;
    }

    SDL_GL_SetSwapInterval(0);

    if (!gladLoadGL(
            reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)))
    {
        SDL_Log("Failed to load OpenGL functions");
        releasePlatformResources();
        return;
    }

    m_glLoaded = true;

    if (!GLAD_GL_VERSION_4_3)
    {
        SDL_Log("OpenGL 4.3 or newer is required by the compute backend");
        releasePlatformResources();
        return;
    }

    SDL_Log("OpenGL: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    SDL_Log("GLSL: %s", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
    SDL_Log("GPU: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    glViewport(0, 0, m_width, m_height);
    glEnable(GL_PROGRAM_POINT_SIZE);

    m_lastCounter = SDL_GetPerformanceCounter();
    m_valid = true;
}


SDLOpenGLRenderer::~SDLOpenGLRenderer()
{
    if (m_window && m_context)
    {
        SDL_GL_MakeCurrent(
            m_window,
            reinterpret_cast<SDL_GLContext>(m_context)
        );
    }

    if (m_glLoaded)
    {
        for (auto [program, glProgram] : m_programs)
        {
            static_cast<void>(program);
            glDeleteProgram(glProgram);
        }
        m_programs.clear();

        if (m_vao)
        {
            glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }

        if (m_pointProgram) { glDeleteProgram(m_pointProgram); m_pointProgram = 0; }
        if (m_pointVAO) { glDeleteVertexArrays(1, &m_pointVAO); m_pointVAO = 0; }
        if (m_pointVBO) { glDeleteBuffers(1, &m_pointVBO); m_pointVBO = 0; }

        if (m_lineProgram) { glDeleteProgram(m_lineProgram); m_lineProgram = 0; }
        if (m_lineVAO) { glDeleteVertexArrays(1, &m_lineVAO); m_lineVAO = 0; }
        if (m_lineVBO) { glDeleteBuffers(1, &m_lineVBO); m_lineVBO = 0; }

        if (m_circleProgram) { glDeleteProgram(m_circleProgram); m_circleProgram = 0; }
        if (m_circleVAO) { glDeleteVertexArrays(1, &m_circleVAO); m_circleVAO = 0; }
        if (m_circleVBO) { glDeleteBuffers(1, &m_circleVBO); m_circleVBO = 0; }
        if (m_circleEBO) { glDeleteBuffers(1, &m_circleEBO); m_circleEBO = 0; }
        if (m_circleInstanceVBO) { glDeleteBuffers(1, &m_circleInstanceVBO); m_circleInstanceVBO = 0; }

        if (m_rectProgram) { glDeleteProgram(m_rectProgram); m_rectProgram = 0; }
        if (m_rectVAO) { glDeleteVertexArrays(1, &m_rectVAO); m_rectVAO = 0; }
        if (m_rectVBO) { glDeleteBuffers(1, &m_rectVBO); m_rectVBO = 0; }
        if (m_rectEBO) { glDeleteBuffers(1, &m_rectEBO); m_rectEBO = 0; }
        if (m_rectInstanceVBO) { glDeleteBuffers(1, &m_rectInstanceVBO); m_rectInstanceVBO = 0; }

        if (m_textProgram) { glDeleteProgram(m_textProgram); m_textProgram = 0; }
        if (m_textVAO) { glDeleteVertexArrays(1, &m_textVAO); m_textVAO = 0; }
        if (m_textVBO) { glDeleteBuffers(1, &m_textVBO); m_textVBO = 0; }
        if (m_fontTexture) { glDeleteTextures(1, &m_fontTexture); m_fontTexture = 0; }
    }

    releasePlatformResources();
}

void SDLOpenGLRenderer::releasePlatformResources() noexcept
{
    m_valid = false;
    m_glLoaded = false;

    if (m_context)
    {
        SDL_GL_DestroyContext(
            reinterpret_cast<SDL_GLContext>(m_context)
        );
        m_context = nullptr;
    }

    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    if (m_ownsVideoSubsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_ownsVideoSubsystem = false;
    }
}

bool SDLOpenGLRenderer::processEvents()
{
    if (!m_valid)
        return false;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            return false;
        }

        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            m_width = event.window.data1;
            m_height = event.window.data2;
            glViewport(0, 0, m_width, m_height);
        }
    }

    return true;
}

f32 SDLOpenGLRenderer::frameDeltaSeconds()
{
    const std::uint64_t now = SDL_GetPerformanceCounter();
    const std::uint64_t frequency = SDL_GetPerformanceFrequency();

    if (m_lastCounter == 0 || frequency == 0)
    {
        m_lastCounter = now;
        return 1.0f / 60.0f;
    }

    const f32 delta = static_cast<f32>(now - m_lastCounter) /
                      static_cast<f32>(frequency);
    m_lastCounter = now;
    return delta;
}

void SDLOpenGLRenderer::beginFrame()
{
    if (!m_valid)
        return;

    // One-time capacity reservation. clear() below retains this capacity,
    // so no per-frame CPU allocations occur during normal rendering.
    if (m_rectBatch.capacity() == 0)
    {
        m_pointBatch.reserve(65536);
        m_lineBatch.reserve(65536 * 2);
        m_circleBatch.reserve(4096);
        m_rectBatch.reserve(1048576);
        m_textBatch.reserve(16384);
    }

    // Reset logical batch sizes; keep allocated capacity.
    m_pointBatch.clear();
    m_lineBatch.clear();
    m_circleBatch.clear();
    m_rectBatch.clear();
    m_textBatch.clear();

    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void SDLOpenGLRenderer::drawPoint(f32 x, f32 y, u32 color)
{
    if (!m_valid || m_width <= 0 || m_height <= 0)
        return;

    // Record only. NDC conversion preserved from the previous
    // immediate-mode implementation.
    const float r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float b = static_cast<float>((color >> 8)  & 0xFF) / 255.0f;
    const float a = static_cast<float>( color        & 0xFF) / 255.0f;

    m_pointBatch.push_back({
        x * 2.0f / static_cast<f32>(m_width) - 1.0f,
        y * 2.0f / static_cast<f32>(m_height) - 1.0f,
        r, g, b, a
    });
}

void SDLOpenGLRenderer::drawLine(
    f32 x1, f32 y1, f32 x2, f32 y2, u32 color)
{
    if (!m_valid || m_width <= 0 || m_height <= 0)
        return;

    // Record two vertices (one line) into the CPU-side line batch.
    const float r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float b = static_cast<float>((color >> 8)  & 0xFF) / 255.0f;
    const float a = static_cast<float>( color        & 0xFF) / 255.0f;

    const float invWidth =
        2.0f / static_cast<f32>(m_width);
    const float invHeight =
        2.0f / static_cast<f32>(m_height);

    m_lineBatch.push_back({
        x1 * invWidth - 1.0f,
        y1 * invHeight - 1.0f,
        r, g, b, a
    });
    m_lineBatch.push_back({
        x2 * invWidth - 1.0f,
        y2 * invHeight - 1.0f,
        r, g, b, a
    });
}

void SDLOpenGLRenderer::drawCircle(
    f32 x, f32 y, f32 radius, u32 color)
{
    if (!m_valid || m_width <= 0 || m_height <= 0)
        return;

    if (radius <= 0.0f)
        return;

    // Record a single instance. Pixel-space; the instanced shader
    // converts to NDC using the current viewport on the GPU.
    const float r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float b = static_cast<float>((color >> 8)  & 0xFF) / 255.0f;
    const float a = static_cast<float>( color        & 0xFF) / 255.0f;

    m_circleBatch.push_back({
        x, y, radius, 0.0f,
        r, g, b, a
    });
}

void SDLOpenGLRenderer::drawRect(
    f32 x,
    f32 y,
    f32 width,
    f32 height,
    u32 color
)
{
    if (!m_valid || m_width <= 0 || m_height <= 0)
        return;

    // Record a single instance. Pixel-space; the instanced unit-quad
    // shader converts to NDC using the current viewport on the GPU.
    const float r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float b = static_cast<float>((color >> 8)  & 0xFF) / 255.0f;
    const float a = static_cast<float>( color        & 0xFF) / 255.0f;

    m_rectBatch.push_back({
        x, y, width, height,
        r, g, b, a
    });
}

void SDLOpenGLRenderer::drawText(
    std::string_view text,
    f32 x,
    f32 y,
    f32 size,
    u32 color
)
{
    if (!m_valid || m_width <= 0 || m_height <= 0)
        return;

    if (text.empty())
        return;

    constexpr int FontSize = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    constexpr int CharCount = 96;

    const float scale =
        size / static_cast<f32>(FontSize);

    const float charW =
        FontSize * scale * 2.0f /
        static_cast<f32>(m_width);

    const float charH =
        FontSize * scale * 2.0f /
        static_cast<f32>(m_height);

    // SDL-style coordinates:
    // (0, 0) = top-left.
    float cursorX =
        x * 2.0f /
        static_cast<f32>(m_width) - 1.0f;

    float cursorY =
        1.0f -
        y * 2.0f /
        static_cast<f32>(m_height);

    const float r =
        static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float g =
        static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float b =
        static_cast<float>((color >> 8)  & 0xFF) / 255.0f;
    const float a =
        static_cast<float>( color        & 0xFF) / 255.0f;

    constexpr float vTop = 0.0f;
    constexpr float vBottom = 1.0f;

    // Append six textured vertices per glyph to the CPU-side text batch.
    // No GPU work happens here; the whole batch is submitted in endFrame().
    for (char c : text)
    {
        const int idx =
            static_cast<int>(
                static_cast<unsigned char>(c)) - 32;

        const int glyphIndex =
            (idx >= 0 && idx < CharCount)
                ? idx
                : 0;

        const float u0 =
            static_cast<float>(glyphIndex) /
            static_cast<float>(CharCount);

        const float u1 =
            static_cast<float>(glyphIndex + 1) /
            static_cast<float>(CharCount);

        const float y0 = cursorY;
        const float y1 = cursorY - charH;

        m_textBatch.push_back({cursorX,          y0, u0, vTop,    r, g, b, a});
        m_textBatch.push_back({cursorX + charW,  y0, u1, vTop,    r, g, b, a});
        m_textBatch.push_back({cursorX + charW,  y1, u1, vBottom, r, g, b, a});

        m_textBatch.push_back({cursorX,          y0, u0, vTop,    r, g, b, a});
        m_textBatch.push_back({cursorX + charW,  y1, u1, vBottom, r, g, b, a});
        m_textBatch.push_back({cursorX,          y1, u0, vBottom, r, g, b, a});

        cursorX += charW;
    }
}

void SDLOpenGLRenderer::endFrame()
{
    if (!m_valid)
        return;

    // Deferred GPU submission in deterministic render order:
    // points -> lines -> circles -> rectangles -> text.
    flushPointBatch();
    flushLineBatch();
    flushCircleBatch();
    flushRectBatch();
    flushTextBatch();

    SDL_GL_SwapWindow(m_window);
}

// ============================================================================
// POINTS
// ============================================================================

const char* SDLOpenGLRenderer::pointVertexSource()
{
    return R"GLSL(
        #version 430 core
        layout(location = 0) in vec2 position;
        layout(location = 1) in vec4 color;
        layout(location = 0) out vec4 vertexColor;
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            gl_PointSize = 1.0;
            vertexColor = color;
        }
    )GLSL";
}

const char* SDLOpenGLRenderer::pointFragmentSource()
{
    return R"GLSL(
        #version 430 core
        layout(location = 0) out vec4 fragColor;
        layout(location = 0) in vec4 vertexColor;
        void main()
        {
            fragColor = vertexColor;
        }
    )GLSL";
}

void SDLOpenGLRenderer::ensurePointResources()
{
    if (m_pointProgram)
        return;

    GLuint vs = compileShader(GL_VERTEX_SHADER, pointVertexSource());
    if (!vs)
        return;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, pointFragmentSource());
    if (!fs)
    {
        glDeleteShader(vs);
        return;
    }

    GLuint program = linkShaderProgram(vs, fs);
    if (!program)
        return;

    m_pointProgram = program;

    glGenVertexArrays(1, &m_pointVAO);
    glGenBuffers(1, &m_pointVBO);
    setupPosColorAttributes(m_pointVAO, m_pointVBO);
}

void SDLOpenGLRenderer::flushPointBatch()
{
    if (m_pointBatch.empty())
        return;

    ensurePointResources();
    if (!m_pointProgram)
    {
        m_pointBatch.clear();
        return;
    }

    const GLsizeiptr byteSize =
        static_cast<GLsizeiptr>(
            m_pointBatch.size() * sizeof(BatchPoint));

    // One upload + one draw call for the entire point batch.
    glUseProgram(m_pointProgram);
    glBindVertexArray(m_pointVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_pointVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        byteSize,
        m_pointBatch.data(),
        GL_STREAM_DRAW);
    glDrawArrays(
        GL_POINTS,
        0,
        static_cast<GLsizei>(m_pointBatch.size()));

    glBindVertexArray(0);
    glUseProgram(0);
}

// ============================================================================
// LINES
// ============================================================================

const char* SDLOpenGLRenderer::lineVertexSource()
{
    // Lines use the same vertex format as points.
    return pointVertexSource();
}

const char* SDLOpenGLRenderer::lineFragmentSource()
{
    return pointFragmentSource();
}

void SDLOpenGLRenderer::ensureLineResources()
{
    if (m_lineProgram)
        return;

    GLuint vs = compileShader(GL_VERTEX_SHADER, lineVertexSource());
    if (!vs)
        return;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, lineFragmentSource());
    if (!fs)
    {
        glDeleteShader(vs);
        return;
    }

    GLuint program = linkShaderProgram(vs, fs);
    if (!program)
        return;

    m_lineProgram = program;

    glGenVertexArrays(1, &m_lineVAO);
    glGenBuffers(1, &m_lineVBO);
    setupPosColorAttributes(m_lineVAO, m_lineVBO);
}

void SDLOpenGLRenderer::flushLineBatch()
{
    if (m_lineBatch.empty())
        return;

    ensureLineResources();
    if (!m_lineProgram)
    {
        m_lineBatch.clear();
        return;
    }

    const GLsizeiptr byteSize =
        static_cast<GLsizeiptr>(
            m_lineBatch.size() * sizeof(BatchPoint));

    // One upload + one draw call for the entire line batch.
    glUseProgram(m_lineProgram);
    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        byteSize,
        m_lineBatch.data(),
        GL_STREAM_DRAW);
    glDrawArrays(
        GL_LINES,
        0,
        static_cast<GLsizei>(m_lineBatch.size()));

    glBindVertexArray(0);
    glUseProgram(0);
}

// ============================================================================
// CIRCLES
// ============================================================================

const char* SDLOpenGLRenderer::circleVertexSource()
{
    return R"GLSL(
        #version 430 core

        layout(location = 0) in vec2 position;      // unit-circle mesh
        layout(location = 1) in vec4 instanceData;  // x, y, radius, padding (pixels)
        layout(location = 2) in vec4 instanceColor; // r, g, b, a

        layout(location = 0) out vec4 vertexColor;

        uniform vec2 viewScale; // 2 / width, 2 / height

        void main()
        {
            // (0,0) = top-left in pixel space.
            float ndcX = instanceData.x * viewScale.x - 1.0;
            float ndcY = 1.0 - instanceData.y * viewScale.y;

            // Unit-circle offset scaled by the instance radius. The X and Y
            // scale factors differ because NDC is not isotropic with pixels.
            vec2 offset = vec2(
                position.x * instanceData.z * viewScale.x,
                position.y * instanceData.z * viewScale.y
            );

            gl_Position = vec4(ndcX + offset.x, ndcY + offset.y, 0.0, 1.0);
            vertexColor = instanceColor;
        }
    )GLSL";
}

const char* SDLOpenGLRenderer::circleFragmentSource()
{
    return R"GLSL(
        #version 430 core

        layout(location = 0) in vec4 vertexColor;
        layout(location = 0) out vec4 fragColor;

        void main()
        {
            fragColor = vertexColor;
        }
    )GLSL";
}

void SDLOpenGLRenderer::ensureCircleResources()
{
    if (m_circleProgram)
        return;

    GLuint vs = compileShader(GL_VERTEX_SHADER, circleVertexSource());
    if (!vs)
        return;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, circleFragmentSource());
    if (!fs)
    {
        glDeleteShader(vs);
        return;
    }

    GLuint program = linkShaderProgram(vs, fs);
    if (!program)
        return;

    m_circleProgram = program;
    m_circleViewScaleLoc =
        glGetUniformLocation(m_circleProgram, "viewScale");

    // Shared unit-circle mesh (center + 64 perimeter vertices).
    // Built once and reused every frame.
    constexpr int Segments = 64;
    std::vector<float> vertices((Segments + 2) * 2, 0.0f);
    for (int i = 0; i <= Segments; ++i)
    {
        const float angle =
            static_cast<float>(i) / static_cast<float>(Segments) *
            6.28318530718f;
        vertices[(i + 1) * 2 + 0] = std::cos(angle);
        vertices[(i + 1) * 2 + 1] = std::sin(angle);
    }

    std::vector<GLuint> indices(static_cast<std::size_t>(Segments) * 3);
    for (int i = 0; i < Segments; ++i)
    {
        indices[static_cast<std::size_t>(i) * 3 + 0] = 0;
        indices[static_cast<std::size_t>(i) * 3 + 1] =
            static_cast<GLuint>(i + 1);
        indices[static_cast<std::size_t>(i) * 3 + 2] =
            static_cast<GLuint>(i + 2);
    }

    glGenVertexArrays(1, &m_circleVAO);
    glGenBuffers(1, &m_circleVBO);
    glGenBuffers(1, &m_circleEBO);
    glGenBuffers(1, &m_circleInstanceVBO);

    glBindVertexArray(m_circleVAO);

    // Static geometry: created once, reused every frame.
    glBindBuffer(GL_ARRAY_BUFFER, m_circleVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
        vertices.data(),
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_circleEBO);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)),
        indices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        2 * sizeof(float),
        reinterpret_cast<const void*>(0));

    // Per-instance data: BatchCircle (8 floats, 32 bytes).
    //   location 1: x, y, radius, padding
    //   location 2: r, g, b, a
    glBindBuffer(GL_ARRAY_BUFFER, m_circleInstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei instanceStride = 8 * sizeof(float);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE,
        instanceStride,
        reinterpret_cast<const void*>(0));
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE,
        instanceStride,
        reinterpret_cast<const void*>(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(0);
}

void SDLOpenGLRenderer::flushCircleBatch()
{
    if (m_circleBatch.empty())
        return;

    ensureCircleResources();
    if (!m_circleProgram)
    {
        m_circleBatch.clear();
        return;
    }

    const GLsizeiptr byteSize =
        static_cast<GLsizeiptr>(
            m_circleBatch.size() * sizeof(BatchCircle));

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_circleProgram);
    glUniform2f(
        m_circleViewScaleLoc,
        2.0f / static_cast<GLfloat>(m_width),
        2.0f / static_cast<GLfloat>(m_height));

    glBindVertexArray(m_circleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_circleInstanceVBO);

    // One upload + one instanced draw call for the entire circle batch.
    glBufferData(
        GL_ARRAY_BUFFER,
        byteSize,
        m_circleBatch.data(),
        GL_STREAM_DRAW);

    glDrawElementsInstanced(
        GL_TRIANGLES,
        static_cast<GLsizei>(64 * 3),
        GL_UNSIGNED_INT,
        nullptr,
        static_cast<GLsizei>(m_circleBatch.size()));

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

// ============================================================================
// RECTANGLES
// ============================================================================

const char* SDLOpenGLRenderer::rectVertexSource()
{
    return R"GLSL(
        #version 430 core

        layout(location = 0) in vec2 position;      // unit quad
        layout(location = 1) in vec4 instanceData;  // x, y, width, height (pixels)
        layout(location = 2) in vec4 instanceColor; // r, g, b, a

        layout(location = 0) out vec4 vertexColor;

        uniform vec2 viewport; // width, height in pixels

        void main()
        {
            // (0,0) = top-left in pixel space.
            vec2 pixel = instanceData.xy + position * instanceData.zw;
            vec2 ndc = vec2(
                pixel.x * 2.0 / viewport.x - 1.0,
                1.0 - pixel.y * 2.0 / viewport.y
            );

            gl_Position = vec4(ndc, 0.0, 1.0);
            vertexColor = instanceColor;
        }
    )GLSL";
}

const char* SDLOpenGLRenderer::rectFragmentSource()
{
    return R"GLSL(
        #version 430 core

        layout(location = 0) in vec4 vertexColor;
        layout(location = 0) out vec4 fragColor;

        void main()
        {
            fragColor = vertexColor;
        }
    )GLSL";
}

void SDLOpenGLRenderer::ensureRectResources()
{
    if (m_rectProgram)
        return;

    GLuint vs = compileShader(GL_VERTEX_SHADER, rectVertexSource());
    if (!vs)
        return;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, rectFragmentSource());
    if (!fs)
    {
        glDeleteShader(vs);
        return;
    }

    GLuint program = linkShaderProgram(vs, fs);
    if (!program)
        return;

    m_rectProgram = program;
    m_rectViewportLoc =
        glGetUniformLocation(m_rectProgram, "viewport");

    // Shared unit quad [0,0]..[1,1]. Created once, reused every frame.
    constexpr float quadVertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };
    constexpr GLuint quadIndices[] = {
        0, 1, 2,
        0, 2, 3
    };

    glGenVertexArrays(1, &m_rectVAO);
    glGenBuffers(1, &m_rectVBO);
    glGenBuffers(1, &m_rectEBO);
    glGenBuffers(1, &m_rectInstanceVBO);

    glBindVertexArray(m_rectVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_rectVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(quadVertices),
        quadVertices,
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_rectEBO);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(quadIndices),
        quadIndices,
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        2 * sizeof(float),
        reinterpret_cast<const void*>(0));

    // Per-instance data: BatchRect (8 floats, 32 bytes).
    //   location 1: x, y, width, height
    //   location 2: r, g, b, a
    glBindBuffer(GL_ARRAY_BUFFER, m_rectInstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei instanceStride = 8 * sizeof(float);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE,
        instanceStride,
        reinterpret_cast<const void*>(0));
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE,
        instanceStride,
        reinterpret_cast<const void*>(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(0);
}

void SDLOpenGLRenderer::flushRectBatch()
{
    if (m_rectBatch.empty())
        return;

    ensureRectResources();
    if (!m_rectProgram)
    {
        m_rectBatch.clear();
        return;
    }

    const GLsizeiptr byteSize =
        static_cast<GLsizeiptr>(
            m_rectBatch.size() * sizeof(BatchRect));

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_rectProgram);
    glUniform2f(
        m_rectViewportLoc,
        static_cast<GLfloat>(m_width),
        static_cast<GLfloat>(m_height));

    glBindVertexArray(m_rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_rectInstanceVBO);

    // One upload + one instanced draw call for the entire rectangle batch.
    glBufferData(
        GL_ARRAY_BUFFER,
        byteSize,
        m_rectBatch.data(),
        GL_STREAM_DRAW);

    glDrawElementsInstanced(
        GL_TRIANGLES,
        6,
        GL_UNSIGNED_INT,
        nullptr,
        static_cast<GLsizei>(m_rectBatch.size()));

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

// ============================================================================
// TEXT
// ============================================================================

const char* SDLOpenGLRenderer::textVertexSource()
{
    return R"GLSL(
        #version 430 core
        layout(location = 0) in vec2 position;
        layout(location = 1) in vec2 texCoord;
        layout(location = 2) in vec4 color;
        layout(location = 0) out vec2 fragTexCoord;
        layout(location = 1) out vec4 vertexColor;
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            fragTexCoord = texCoord;
            vertexColor = color;
        }
    )GLSL";
}

const char* SDLOpenGLRenderer::textFragmentSource()
{
    return R"GLSL(
        #version 430 core

        layout(location = 0) in vec2 fragTexCoord;
        layout(location = 1) in vec4 vertexColor;
        layout(location = 0) out vec4 fragColor;

        uniform sampler2D fontTexture;

        void main()
        {
            float alpha = texture(fontTexture, fragTexCoord).a;
            fragColor = vec4(vertexColor.rgb, vertexColor.a * alpha);
        }
    )GLSL";
}

void SDLOpenGLRenderer::ensureTextResources()
{
    if (m_textProgram)
        return;

    if (!m_glLoaded)
        return;

    constexpr int FontSize = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    constexpr int CharCount = 96; // ASCII 32..127
    const int atlasWidth  = FontSize * CharCount;
    const int atlasHeight = FontSize;

    // ------------------------------------------------------------
    // Build a font atlas using SDL's built-in 8x8 debug font.
    // Render each printable ASCII glyph onto an SDL surface via a
    // software renderer, then upload the result as a GL texture.
    // ------------------------------------------------------------
    SDL_Surface* surface = SDL_CreateSurface(
        atlasWidth, atlasHeight, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return;

    SDL_Renderer* swRenderer = SDL_CreateSoftwareRenderer(surface);
    if (!swRenderer)
    {
        SDL_DestroySurface(surface);
        return;
    }

    SDL_SetRenderDrawColor(swRenderer, 0, 0, 0, 0);
    SDL_RenderClear(swRenderer);

    SDL_SetRenderDrawColor(swRenderer, 255, 255, 255, 255);
    for (int i = 0; i < CharCount; ++i)
    {
        char glyph[2] = {
            static_cast<char>(' ' + i),
            '\0'
        };

        SDL_RenderDebugText(
            swRenderer,
            static_cast<float>(i * FontSize),
            0.0f,
            glyph);
    }
    SDL_RenderPresent(swRenderer);

    // Scale the atlas surface up so glyphs are readable when magnified.
    int uploadWidth = atlasWidth;
    int uploadHeight = atlasHeight;
    SDL_Surface* scaledSurface = SDL_ScaleSurface(
        surface,
        atlasWidth * 4,
        atlasHeight * 4,
        SDL_SCALEMODE_NEAREST
    );
    if (scaledSurface)
    {
        SDL_DestroySurface(surface);
        surface = scaledSurface;
        uploadWidth = atlasWidth * 4;
        uploadHeight = atlasHeight * 4;
    }

    glGenTextures(1, &m_fontTexture);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA,
        uploadWidth, uploadHeight, 0,
        GL_RGBA, GL_UNSIGNED_BYTE,
        surface->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    SDL_DestroyRenderer(swRenderer);
    SDL_DestroySurface(surface);

    // ------------------------------------------------------------
    // Compile the text shader.
    // ------------------------------------------------------------
    GLuint vs = compileShader(GL_VERTEX_SHADER, textVertexSource());
    if (!vs)
        return;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, textFragmentSource());
    if (!fs)
    {
        glDeleteShader(vs);
        return;
    }

    GLuint program = linkShaderProgram(vs, fs);
    if (!program)
        return;

    m_textProgram = program;

    // Sampler binding is constant; set it once at creation.
    glUseProgram(m_textProgram);
    glUniform1i(
        glGetUniformLocation(m_textProgram, "fontTexture"),
        0);
    glUseProgram(0);

    glGenVertexArrays(1, &m_textVAO);
    glGenBuffers(1, &m_textVBO);
    glBindVertexArray(m_textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVBO);

    // BatchTextVertex: position (2), texCoord (2), color (4) = 8 floats.
    constexpr GLsizei stride = 8 * sizeof(float);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        stride,
        reinterpret_cast<const void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE,
        stride,
        reinterpret_cast<const void*>(2 * sizeof(float)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE,
        stride,
        reinterpret_cast<const void*>(4 * sizeof(float)));

    glBindVertexArray(0);
}

void SDLOpenGLRenderer::flushTextBatch()
{
    if (m_textBatch.empty())
        return;

    ensureTextResources();
    if (!m_textProgram || !m_fontTexture)
    {
        m_textBatch.clear();
        return;
    }

    const GLsizeiptr byteSize =
        static_cast<GLsizeiptr>(
            m_textBatch.size() * sizeof(BatchTextVertex));

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_textProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);
    glBindVertexArray(m_textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVBO);

    // One upload + one draw call for the entire text batch
    // (no per-glyph GPU submissions).
    glBufferData(
        GL_ARRAY_BUFFER,
        byteSize,
        m_textBatch.data(),
        GL_STREAM_DRAW);

    glDrawArrays(
        GL_TRIANGLES,
        0,
        static_cast<GLsizei>(m_textBatch.size()));

    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

// ============================================================================
// Compute pipeline (IRendererShaderPipeline) - unchanged immediate path.
// ============================================================================

ProgramHandle SDLOpenGLRenderer::createGraphicsProgram(
    const std::string& vertex,
    const std::string& fragment)
{
    std::string vertexSource = readFile(vertex);
    std::string fragmentSource = readFile(fragment);

    if (vertexSource.empty() || fragmentSource.empty())
    {
        return 0;
    }

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader)
        return 0;

    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader)
    {
        glDeleteShader(vertexShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;

    glGetProgramiv(
        program,
        GL_LINK_STATUS,
        &success
    );

    if (!success)
    {
        GLint length = 0;

        glGetProgramiv(
            program,
            GL_INFO_LOG_LENGTH,
            &length
        );

        std::vector<char> log(length);

        glGetProgramInfoLog(
            program,
            length,
            nullptr,
            log.data()
        );

        std::cerr
            << "Graphics program linking failed:\n"
            << log.data()
            << '\n';

        glDeleteProgram(program);

        return 0;
    }

    ProgramHandle handle = ++m_nextHandle;
    m_programs[handle] = program;

    return handle;
}

void SDLOpenGLRenderer::destroyProgram(
    ProgramHandle program)
{
    auto it = m_programs.find(program);
    if (it != m_programs.end())
    {
        glDeleteProgram(it->second);
        m_programs.erase(it);
    }
}

void SDLOpenGLRenderer::bindProgram(
    ProgramHandle program)
{
    auto it = m_programs.find(program);
    if (it != m_programs.end())
    {
        glUseProgram(it->second);
    }
}

void SDLOpenGLRenderer::bindStorageBuffer(
    u32 binding,
    BufferHandle buffer)
{
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER,
        binding,
        buffer
    );
}

void SDLOpenGLRenderer::drawPoints(
    u32 count)
{
    if (!m_vao)
    {
        glGenVertexArrays(1, &m_vao);
    }

    glBindVertexArray(m_vao);

    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT |
        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
    );

    glDrawArrays(GL_POINTS, 0, count);

    glBindVertexArray(0);
}

// ============================================================================
// Shared helpers
// ============================================================================

std::string SDLOpenGLRenderer::readFile(
    const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    file.seekg(0, std::ios::end);
    std::string content;
    content.resize(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(content.data(), content.size());

    return content;
}

GLuint SDLOpenGLRenderer::compileShader(
    GLenum type,
    const std::string& source)
{
    GLuint shader =
        glCreateShader(type);

    const char* sourceData = source.data();

    GLint sourceLength =
        static_cast<GLint>(source.size());

    glShaderSource(
        shader,
        1,
        &sourceData,
        &sourceLength
    );

    glCompileShader(shader);

    GLint success = GL_FALSE;

    glGetShaderiv(
        shader,
        GL_COMPILE_STATUS,
        &success
    );

    if (!success)
    {
        GLint length = 0;

        glGetShaderiv(
            shader,
            GL_INFO_LOG_LENGTH,
            &length
        );

        std::vector<char> log(length);

        glGetShaderInfoLog(
            shader,
            length,
            nullptr,
            log.data()
        );

        std::cerr
            << "Shader compilation failed:\n"
            << log.data()
            << '\n';

        glDeleteShader(shader);

        return 0;
    }

    return shader;
}

} // namespace emper::backend
