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

    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void SDLOpenGLRenderer::drawPoint(f32 x, f32 y, u32 color)
{
    // The OpenGL backend currently renders sample geometry through the
    // explicit graphics pipeline API below. Generic debug primitives are
    // intentionally left unsupported until that pipeline is generalized.
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(color);
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

void SDLOpenGLRenderer::endFrame()
{
    if (m_valid)
        SDL_GL_SwapWindow(m_window);
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