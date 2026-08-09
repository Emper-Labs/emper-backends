#pragma once

#include <emper/ComputeTypes.h>
#include <emper/interfaces/backend/ICompute.h>

#include <unordered_map>

namespace emper::backend
{

class OpenGLComputeBackend final
    : public emper::interfaces::backend::IGPUComputeBackend
{
public:
    bool initialize() override;
    void shutdown() override;

    ProgramHandle createProgram(
        std::string_view source
    ) override;

    ProgramHandle compileShader(
        const std::string& path
    ) override;

    void destroyProgram(
        ProgramHandle program
    ) override;

    BufferHandle createBuffer(
        const BufferDesc& desc
    ) override;

    void destroyBuffer(
        BufferHandle buffer
    ) override;

    bool writeBuffer(
        BufferHandle buffer,
        const void* data,
        std::size_t size
    ) override;

    bool readBuffer(
        BufferHandle buffer,
        void* data,
        std::size_t size
    ) override;

    void bindStorageBuffer(
        std::uint32_t binding,
        BufferHandle buffer
    ) override;

    void dispatch(
        ProgramHandle program,
        DispatchSize size
    ) override;

    void setUniform1f(
        ProgramHandle program,
        const std::string& name,
        float value
    ) override;

    void setUniform1i(
        ProgramHandle program,
        const std::string& name,
        int value
    ) override;

    void memoryBarrier() override;
};

}