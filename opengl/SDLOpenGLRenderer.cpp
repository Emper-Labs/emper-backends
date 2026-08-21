#include "SDLOpenGLRenderer.h"

#include <fstream>
#include <iostream>
#include <vector>

#include <SDL3/SDL.h>

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

        if (m_vao)
        {
            glDeleteVertexArrays(1, &m_vao);
        }
    }

    m_programs.clear();
    m_vao = 0;

    if (m_textProgram)
    {
        glDeleteProgram(m_textProgram);
        m_textProgram = 0;
    }
    if (m_textVAO)
    {
        glDeleteVertexArrays(1, &m_textVAO);
        m_textVAO = 0;
    }
    if (m_textVBO)
    {
        glDeleteBuffers(1, &m_textVBO);
        m_textVBO = 0;
    }
    if (m_fontTexture)
    {
        glDeleteTextures(1, &m_fontTexture);
        m_fontTexture = 0;
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

    m_pointBatch.clear();

    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void SDLOpenGLRenderer::drawPoint(f32 x, f32 y, u32 color)
{
    if (!m_valid || m_width <= 0 || m_height <= 0)
        return;

    ensurePointResources();

    // Batch in immediate-mode. Each point is 6 floats:
    //   normalizedX, normalizedY, r, g, b, a
    m_pointBatch.push_back(x * 2.0f / m_width - 1.0f);
    m_pointBatch.push_back(y * 2.0f / m_height - 1.0f);
    m_pointBatch.push_back(((color >> 24) & 0xFF) / 255.0f);
    m_pointBatch.push_back(((color >> 16) & 0xFF) / 255.0f);
    m_pointBatch.push_back(((color >>  8) & 0xFF) / 255.0f);
    m_pointBatch.push_back(((color >>  0) & 0xFF) / 255.0f);
}

void SDLOpenGLRenderer::drawLine(
    f32 x1, f32 y1, f32 x2, f32 y2, u32 color)
{
    static_cast<void>(x1);
    static_cast<void>(y1);
    static_cast<void>(x2);
    static_cast<void>(y2);
    static_cast<void>(color);
}

void SDLOpenGLRenderer::drawCircle(
    f32 x, f32 y, f32 radius, u32 color)
{
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(radius);
    static_cast<void>(color);
}

void SDLOpenGLRenderer::drawText(
    std::string_view text,
    f32 x,
    f32 y,
    f32 size)
{
    ensureTextResources();

    if (!m_textProgram || !m_fontTexture)
        return;

    if (m_width <= 0 || m_height <= 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_textProgram);

    glUniform1i(
        glGetUniformLocation(m_textProgram, "fontTexture"),
        0);

    glUniform4f(
        glGetUniformLocation(m_textProgram, "textColor"),
        1.0f,
        1.0f,
        1.0f,
        1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);

    glBindVertexArray(m_textVAO);

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
        
        //Chỗ này không để ý text ngược mãi mới sửa được 
        constexpr float vTop = 0.0f;
        constexpr float vBottom = 1.0f;

        const float y0 = cursorY;
        const float y1 = cursorY - charH;

        const float vertices[6][4] = {
            // position                  // UV
            {cursorX,          y0,        u0, vTop},
            {cursorX + charW,  y0,        u1, vTop},
            {cursorX + charW,  y1,        u1, vBottom},

            {cursorX,          y0,        u0, vTop},
            {cursorX + charW,  y1,        u1, vBottom},
            {cursorX,          y1,        u0, vBottom},
        };

        glBindBuffer(GL_ARRAY_BUFFER, m_textVBO);

        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            sizeof(vertices),
            vertices);

        glDrawArrays(
            GL_TRIANGLES,
            0,
            6);

        cursorX += charW;
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_BLEND);
}

void SDLOpenGLRenderer::endFrame()
{
    if (!m_valid)
        return;

    if (!m_pointBatch.empty())
        flushPointBatch();

    SDL_GL_SwapWindow(m_window);
}

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

    auto compile = [this](GLenum type, const char* source)
    {
        return compileShader(type, source);
    };

    GLuint vs = compile(GL_VERTEX_SHADER, pointVertexSource());
    if (!vs)
        return;

    GLuint fs = compile(GL_FRAGMENT_SHADER, pointFragmentSource());
    if (!fs)
    {
        glDeleteShader(vs);
        return;
    }

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
        return;
    }

    m_pointProgram = program;

    glGenVertexArrays(1, &m_pointVAO);
    glGenBuffers(1, &m_pointVBO);

    glBindVertexArray(m_pointVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_pointVBO);

    // Two attributes in one interleaved vertex:
    //   floats 0-1: position
    //   floats 2-5: color (RGBA)
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

void SDLOpenGLRenderer::flushPointBatch()
{
    ensurePointResources();
    if (!m_pointProgram || m_pointBatch.empty())
        return;

    const GLsizeiptr byteSize =
        static_cast<GLsizeiptr>(
            m_pointBatch.size() * sizeof(float));

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
        static_cast<GLsizei>(m_pointBatch.size() / 6));

    glBindVertexArray(0);
    glUseProgram(0);
}

const char* SDLOpenGLRenderer::textVertexSource()
{
    return R"GLSL(
        #version 430 core
        layout(location = 0) in vec2 position;
        layout(location = 1) in vec2 texCoord;
        layout(location = 0) out vec2 fragTexCoord;
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            fragTexCoord = texCoord;
        }
    )GLSL";
}

const char* SDLOpenGLRenderer::textFragmentSource()
{
    return R"GLSL(
        #version 430 core

        layout(location = 0) in vec2 fragTexCoord;
        layout(location = 0) out vec4 fragColor;

        uniform sampler2D fontTexture;
        uniform vec4 textColor;

        void main()
        {
            float alpha = texture(fontTexture, fragTexCoord).a;
            fragColor = vec4(textColor.rgb, textColor.a * alpha);
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
    // Use a taller atlas so glyphs are not compressed vertically.
    // SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE is 8, so atlasHeight = 8.
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

    // Clear to fully transparent.
    SDL_SetRenderDrawColor(swRenderer, 0, 0, 0, 0);
    SDL_RenderClear(swRenderer);

    // Render white glyphs (alpha channel will be used by the text shader).
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
    SDL_RenderPresent(swRenderer);//<- quên cái này thì không thấy text đâu ;)

    // --- NEW: scale the atlas surface up so glyphs aren't tiny ---
    const int scaledWidth = atlasWidth * 4;
    const int scaledHeight = atlasHeight * 4;
    SDL_Surface* scaledSurface = SDL_ScaleSurface(
        surface,
        scaledWidth,
        scaledHeight,
        SDL_SCALEMODE_NEAREST
    );
    if (!scaledSurface)
    {
        // Fallback: keep original surface if scaling fails.
    }
    else
    {
        SDL_DestroySurface(surface);
        surface = scaledSurface;
    }

    glGenTextures(1, &m_fontTexture);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int uploadWidth = (scaledSurface) ? scaledWidth : atlasWidth;
    const int uploadHeight = (scaledSurface) ? scaledHeight : atlasHeight;

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

    m_textProgram = glCreateProgram();
    glAttachShader(m_textProgram, vs);
    glAttachShader(m_textProgram, fs);
    glLinkProgram(m_textProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint success = GL_FALSE;
    glGetProgramiv(m_textProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glDeleteProgram(m_textProgram);
        m_textProgram = 0;
        return;
    }

    // Quad VAO/VBO (filled per-character in drawText).
    glGenVertexArrays(1, &m_textVAO);
    glGenBuffers(1, &m_textVBO);
    glBindVertexArray(m_textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(float) * 4 * 6,
        nullptr,
        GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glBindVertexArray(0);
}

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

}