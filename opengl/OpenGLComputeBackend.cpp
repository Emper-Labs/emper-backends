#include "OpenGLComputeBackend.h"

#include <glad/gl.h>

#include <iostream>
#include <vector>
#include <cstring>

#include <fstream>
#include <optional>
#include <filesystem>

namespace
{

std::string readFile(const std::string& path)
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

std::uint64_t computeHash(const std::string& source)
{
    return std::hash<std::string>{}(source);
}

std::optional<emper::ProgramBinary> loadBinary(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;

    emper::ProgramBinaryFile fileHeader;

    file.read(fileHeader.magic, 4);
    if (!file || std::string(fileHeader.magic, 4) != "EMPR")
        return std::nullopt;

    file.read(reinterpret_cast<char*>(&fileHeader.version), sizeof(fileHeader.version));
    if (!file || fileHeader.version != 1)
        return std::nullopt;

    std::uint32_t hashSize = 0;
    file.read(reinterpret_cast<char*>(&hashSize), sizeof(hashSize));
    if (!file || hashSize > 1024)
        return std::nullopt;

    fileHeader.hash.resize(hashSize);
    file.read(fileHeader.hash.data(), hashSize);
    if (!file)
        return std::nullopt;

    file.read(reinterpret_cast<char*>(&fileHeader.format), sizeof(fileHeader.format));
    if (!file)
        return std::nullopt;

    std::uint32_t dataSize = 0;
    file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
    if (!file || dataSize > 4 * 1024 * 1024)
        return std::nullopt;

    fileHeader.data.resize(dataSize);
    file.read(reinterpret_cast<char*>(fileHeader.data.data()), dataSize);
    if (!file)
        return std::nullopt;

    return emper::ProgramBinary{
        fileHeader.hash,
        fileHeader.format,
        fileHeader.data
    };
}

bool saveBinary(const std::string& path, const emper::ProgramBinary& binary)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;

    emper::ProgramBinaryFile fileHeader;
    fileHeader.hash = binary.hash;
    fileHeader.format = binary.format;
    fileHeader.data = binary.data;

    file.write(fileHeader.magic, 4);
    file.write(reinterpret_cast<const char*>(&fileHeader.version), sizeof(fileHeader.version));

    std::uint32_t hashSize = static_cast<std::uint32_t>(fileHeader.hash.size());
    file.write(reinterpret_cast<const char*>(&hashSize), sizeof(hashSize));
    file.write(fileHeader.hash.data(), hashSize);

    file.write(reinterpret_cast<const char*>(&fileHeader.format), sizeof(fileHeader.format));

    std::uint32_t dataSize = static_cast<std::uint32_t>(fileHeader.data.size());
    file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
    file.write(reinterpret_cast<const char*>(fileHeader.data.data()), dataSize);

    return file.good();
}

GLuint loadProgramBinary(GLenum format, const std::vector<std::byte>& data)
{
    GLuint program = glCreateProgram();

    glProgramBinary(
        program,
        format,
        data.data(),
        static_cast<GLsizei>(data.size())
    );

    GLint success = GL_FALSE;

    glGetProgramiv(
        program,
        GL_LINK_STATUS,
        &success
    );

    if (success)
        return program;

    glDeleteProgram(program);

    return 0;
}

GLuint compileComputeShader(
    std::string_view source)
{
    GLuint shader =
        glCreateShader(GL_COMPUTE_SHADER);

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
            << "Compute shader compilation failed:\n"
            << log.data()
            << '\n';

        glDeleteShader(shader);

        return 0;
    }

    return shader;
}

}

namespace emper::backend
{

ProgramHandle OpenGLComputeBackend::compileShader(
    const std::string& path)
{
    std::string source = readFile(path);
    if (source.empty())
    {
        std::cerr << "Failed to read shader file: " << path << '\n';
        return 0;
    }

    std::string hashStr = std::to_string(computeHash(source));

    std::filesystem::path cachePath = std::filesystem::path("cache") / "shaders" / (std::filesystem::path(path).stem().string() + ".bin");

    if (auto binary = loadBinary(cachePath.string()))
    {
        if (binary->hash == hashStr)
        {
            GLuint program = loadProgramBinary(
                binary->format,
                binary->data
            );

            if (program)
            {
                std::cout << "Loaded cached shader: " << cachePath << '\n';
                return program;
            }
        }
    }

    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* sourceData = source.data();
    GLint sourceLength = static_cast<GLint>(source.size());

    glShaderSource(shader, 1, &sourceData, &sourceLength);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(length);
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        std::cerr << "Compute shader compilation failed:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);

    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(length);
        glGetProgramInfoLog(program, length, nullptr, log.data());
        std::cerr << "Compute program linking failed:\n" << log.data() << '\n';
        glDeleteProgram(program);
        return 0;
    }

    GLenum binaryFormat = 0;
    GLint binaryLength = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);

    if (binaryLength > 0)
    {
        std::vector<std::byte> binaryData(binaryLength);
        glGetProgramBinary(program, binaryLength, nullptr, &binaryFormat, binaryData.data());

        emper::ProgramBinary binary = {
            hashStr,
            binaryFormat,
            std::move(binaryData)
        };

        saveBinary(cachePath.string(), binary);
        std::cout << "Cached shader binary: " << cachePath << '\n';
    }

    return program;
}

ProgramHandle OpenGLComputeBackend::createProgram(
    std::string_view source)
{
    GLuint shader =
        compileComputeShader(source);

    if (!shader)
        return 0;

    GLuint program =
        glCreateProgram();

    glAttachShader(program, shader);
    glLinkProgram(program);

    glDeleteShader(shader);

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
            << "Compute program linking failed:\n"
            << log.data()
            << '\n';

        glDeleteProgram(program);

        return 0;
    }

    return program;
}

BufferHandle OpenGLComputeBackend::createBuffer(
    const BufferDesc& desc)
{
    GLuint buffer = 0;

    glGenBuffers(
        1,
        &buffer
    );

    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        buffer
    );

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(desc.size),
        nullptr,
        GL_DYNAMIC_COPY
    );

    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        0
    );

    return buffer;
}

bool OpenGLComputeBackend::initialize()
{
    if (!GLAD_GL_VERSION_4_3 || glGetString(GL_VERSION) == nullptr)
    {
        std::cerr
            << "OpenGLComputeBackend requires an active OpenGL 4.3 context. "
            << "Create SDLOpenGLRenderer before initializing compute.\n";
        return false;
    }

    return true;
}

void OpenGLComputeBackend::shutdown()
{
}

bool OpenGLComputeBackend::writeBuffer(
    BufferHandle buffer,
    const void* data,
    std::size_t size)
{
    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        buffer
    );

    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        static_cast<GLsizeiptr>(size),
        data
    );

    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        0
    );

    return true;
}

bool OpenGLComputeBackend::readBuffer(
    BufferHandle buffer,
    void* data,
    std::size_t size)
{
    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        buffer
    );

    void* mapped = glMapBufferRange(
        GL_SHADER_STORAGE_BUFFER,
        0,
        static_cast<GLsizeiptr>(size),
        GL_MAP_READ_BIT
    );

    if (!mapped)
    {
        glBindBuffer(
            GL_SHADER_STORAGE_BUFFER,
            0
        );

        return false;
    }

    std::memcpy(
        data,
        mapped,
        size
    );

    glUnmapBuffer(
        GL_SHADER_STORAGE_BUFFER
    );

    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        0
    );

    return true;
}

void OpenGLComputeBackend::destroyProgram(
    ProgramHandle program)
{
    glDeleteProgram(program);
}

void OpenGLComputeBackend::destroyBuffer(
    BufferHandle buffer)
{
    glDeleteBuffers(1, &buffer);
}

void OpenGLComputeBackend::bindStorageBuffer(
    std::uint32_t binding,
    BufferHandle buffer)
{
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER,
        binding,
        buffer
    );
}

void OpenGLComputeBackend::dispatch(
    ProgramHandle program,
    DispatchSize size)
{
    glUseProgram(program);

    glDispatchCompute(
        size.x,
        size.y,
        size.z
    );
}

void OpenGLComputeBackend::setUniform1f(
    ProgramHandle program,
    const std::string& name,
    float value)
{
    glUseProgram(program);

    GLint location =
        glGetUniformLocation(
            program,
            name.c_str()
        );

    if (location >= 0)
    {
        glUniform1f(
            location,
            value
        );
    }
}

void OpenGLComputeBackend::setUniform1i(
    ProgramHandle program,
    const std::string& name,
    int value)
{
    glUseProgram(program);

    GLint location =
        glGetUniformLocation(
            program,
            name.c_str()
        );

    if (location >= 0)
    {
        glUniform1i(
            location,
            value
        );
    }
}

void OpenGLComputeBackend::memoryBarrier()
{
    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT
    );
}

}