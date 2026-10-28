// FlinchGpu.h — GPU backends behind one interface.
//
// After Effects picks the GPU framework (CUDA or OpenCL on Windows, Metal on
// macOS); each backend runs the same kernels (flinch_kernels.inc). Buffers are
// the raw GPU memory handles After Effects hands out for its GPU worlds
// (CUdeviceptr, cl_mem or id<MTLBuffer>).
#pragma once

#include <memory>

#include "flinch_gpu_types.h"

namespace flinch::gpu {

class Backend {
public:
    virtual ~Backend() = default;
    // Each call enqueues one pass on the host's queue; false on failure.
    virtual bool geometry(void* src, void* dst, const FlinchGeoParams& p) = 0;
    virtual bool blur(void* src, void* dst, const FlinchBlurParams& p) = 0;
    virtual bool finish(void* geo, void* blurred, void* dst, const FlinchFinishParams& p) = 0;
};

// Each factory returns nullptr if the framework or the kernels are unavailable;
// After Effects then renders Flinch on the CPU.
std::unique_ptr<Backend> make_cuda(void* context, void* stream);
std::unique_ptr<Backend> make_opencl(void* context, void* device, void* queue);
std::unique_ptr<Backend> make_metal(void* device, void* queue);

}  // namespace flinch::gpu
