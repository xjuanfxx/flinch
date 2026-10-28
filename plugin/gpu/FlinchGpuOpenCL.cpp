// FlinchGpuOpenCL.cpp — OpenCL backend (Windows; used by AMD and Intel GPUs, or
// NVIDIA when the project is set to OpenCL).
//
// OpenCL.dll is loaded at runtime (After Effects already has it loaded when it
// renders with OpenCL), so building needs no OpenCL SDK. The kernels are
// compiled from source when the device is set up.
#include "FlinchGpu.h"

#ifdef _WIN32

#include <windows.h>

#include <mutex>
#include <type_traits>

#include "flinch_kernels_opencl.h"  // generated: kFlinchOpenCLSource

namespace flinch::gpu {
namespace {

using cl_int = int;
using cl_uint = unsigned;
using cl_handle = void*;  // cl_context, cl_device_id, cl_command_queue, cl_program, cl_kernel, cl_mem
constexpr cl_int CL_SUCCESS = 0;
constexpr size_t kBlock = 16;

struct OpenCLApi {
    cl_handle(__stdcall* create_program)(cl_handle, cl_uint, const char**, const size_t*, cl_int*) = nullptr;
    cl_int(__stdcall* build_program)(cl_handle, cl_uint, const cl_handle*, const char*, void*, void*) = nullptr;
    cl_handle(__stdcall* create_kernel)(cl_handle, const char*, cl_int*) = nullptr;
    cl_int(__stdcall* set_arg)(cl_handle, cl_uint, size_t, const void*) = nullptr;
    cl_int(__stdcall* enqueue)(cl_handle, cl_handle, cl_uint, const size_t*, const size_t*, const size_t*,
                               cl_uint, const void*, void*) = nullptr;
    cl_int(__stdcall* release_kernel)(cl_handle) = nullptr;
    cl_int(__stdcall* release_program)(cl_handle) = nullptr;

    bool load() {
        HMODULE dll = GetModuleHandleA("OpenCL.dll");
        if (!dll) dll = LoadLibraryA("OpenCL.dll");
        if (!dll) return false;
        auto get = [dll](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(dll, name));
            return fn != nullptr;
        };
        return get(create_program, "clCreateProgramWithSource") && get(build_program, "clBuildProgram") &&
               get(create_kernel, "clCreateKernel") && get(set_arg, "clSetKernelArg") &&
               get(enqueue, "clEnqueueNDRangeKernel") && get(release_kernel, "clReleaseKernel") &&
               get(release_program, "clReleaseProgram");
    }
};

class OpenCLBackend final : public Backend {
public:
    OpenCLBackend(const OpenCLApi& api, cl_handle ctx, cl_handle device, cl_handle queue)
        : api_(api), ctx_(ctx), device_(device), queue_(queue) {}

    ~OpenCLBackend() override {
        for (cl_handle k : {geometry_, blur_, finish_})
            if (k) api_.release_kernel(k);
        if (program_) api_.release_program(program_);
    }

    bool init() {
        cl_int err = CL_SUCCESS;
        const char* source = kFlinchOpenCLSource;
        program_ = api_.create_program(ctx_, 1, &source, nullptr, &err);
        if (err != CL_SUCCESS || !program_) return false;
        if (api_.build_program(program_, 1, &device_, "-cl-fast-relaxed-math", nullptr, nullptr) != CL_SUCCESS)
            return false;
        geometry_ = api_.create_kernel(program_, "flinch_geometry", &err);
        if (err != CL_SUCCESS) return false;
        blur_ = api_.create_kernel(program_, "flinch_blur", &err);
        if (err != CL_SUCCESS) return false;
        finish_ = api_.create_kernel(program_, "flinch_finish", &err);
        return err == CL_SUCCESS;
    }

    bool geometry(void* src, void* dst, const FlinchGeoParams& p) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return arg(geometry_, 0, src) && arg(geometry_, 1, dst) &&
               api_.set_arg(geometry_, 2, sizeof(p), &p) == CL_SUCCESS && run(geometry_, p.out_w, p.out_h);
    }
    bool blur(void* src, void* dst, const FlinchBlurParams& p) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return arg(blur_, 0, src) && arg(blur_, 1, dst) && api_.set_arg(blur_, 2, sizeof(p), &p) == CL_SUCCESS &&
               run(blur_, p.w, p.h);
    }
    bool finish(void* geo, void* blurred, void* dst, const FlinchFinishParams& p) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return arg(finish_, 0, geo) && arg(finish_, 1, blurred) && arg(finish_, 2, dst) &&
               api_.set_arg(finish_, 3, sizeof(p), &p) == CL_SUCCESS && run(finish_, p.w, p.h);
    }

private:
    bool arg(cl_handle kernel, cl_uint index, void* mem) {
        return api_.set_arg(kernel, index, sizeof(cl_handle), &mem) == CL_SUCCESS;
    }
    bool run(cl_handle kernel, int w, int h) {
        const size_t global[2] = {(size_t(w) + kBlock - 1) / kBlock * kBlock,
                                  (size_t(h) + kBlock - 1) / kBlock * kBlock};
        const size_t local[2] = {kBlock, kBlock};
        return api_.enqueue(queue_, kernel, 2, nullptr, global, local, 0, nullptr, nullptr) == CL_SUCCESS;
    }

    const OpenCLApi& api_;
    cl_handle ctx_, device_, queue_;
    cl_handle program_ = nullptr, geometry_ = nullptr, blur_ = nullptr, finish_ = nullptr;
    // Kernel arguments are shared state; renders on several threads must not interleave.
    std::mutex mutex_;
};

}  // namespace

std::unique_ptr<Backend> make_opencl(void* context, void* device, void* queue) {
    static OpenCLApi api;
    static const bool loaded = api.load();
    if (!loaded || !context || !device || !queue) return nullptr;
    auto backend = std::make_unique<OpenCLBackend>(api, context, device, queue);
    if (!backend->init()) return nullptr;
    return backend;
}

}  // namespace flinch::gpu

#else

namespace flinch::gpu {
std::unique_ptr<Backend> make_opencl(void*, void*, void*) { return nullptr; }
}  // namespace flinch::gpu

#endif
