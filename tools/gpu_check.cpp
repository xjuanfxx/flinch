// gpu_check — runs Flinch's GPU kernels on this machine and compares them with
// the CPU renderer, without After Effects.
//
// Creates its own CUDA (driver API) and OpenCL contexts, renders several
// scenarios frame by frame on both paths and reports the pixel difference.
// Static scenarios must match the CPU almost exactly; motion blur scenarios are
// allowed to differ slightly (the CPU uses a box filter along the trail, the GPU
// averages samples).
//
// Usage: gpu_check            (Windows, needs an NVIDIA and/or OpenCL GPU)
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "FlinchGpuRender.h"
#include "flinch_render.h"

namespace {

template <class Fn>
bool load_fn(HMODULE dll, Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(GetProcAddress(dll, name));
    return fn != nullptr;
}

// Converts between the CPU image (RGBA) and the GPU layout (BGRA).
std::vector<float> to_bgra(const flinch::Image& img) {
    std::vector<float> v(img.px.size() * 4);
    for (size_t i = 0; i < img.px.size(); ++i) {
        const flinch::Pixel& p = img.px[i];
        v[4 * i] = p.b; v[4 * i + 1] = p.g; v[4 * i + 2] = p.r; v[4 * i + 3] = p.a;
    }
    return v;
}

flinch::Image from_bgra(const std::vector<float>& v, int w, int h) {
    flinch::Image img(w, h);
    for (size_t i = 0; i < img.px.size(); ++i) img.px[i] = {v[4 * i + 2], v[4 * i + 1], v[4 * i], v[4 * i + 3]};
    return img;
}

// A GPU API seen by the test: memory plus a Flinch backend.
class Device {
public:
    virtual ~Device() = default;
    virtual const char* name() const = 0;
    virtual flinch::gpu::Backend& backend() = 0;
    virtual void* alloc(size_t bytes) = 0;
    virtual void release(void* mem) = 0;
    virtual bool upload(void* mem, const std::vector<float>& data) = 0;
    virtual bool download(void* mem, std::vector<float>& data) = 0;
};

class DeviceScratch final : public flinch::gpu::Scratch {
public:
    explicit DeviceScratch(Device& d) : device_(d) {}
    ~DeviceScratch() override {
        for (void* m : buffers_) device_.release(m);
    }
    bool alloc(int w, int h, flinch::gpu::Buffer& out) override {
        void* m = device_.alloc(size_t(w) * h * 16);
        if (!m) return false;
        buffers_.push_back(m);
        out = {m, w, h, w};
        return true;
    }

private:
    Device& device_;
    std::vector<void*> buffers_;
};

// ---------------------------------------------------------------------------
// CUDA (driver API)
// ---------------------------------------------------------------------------

class CudaDevice final : public Device {
public:
    static std::unique_ptr<CudaDevice> create() {
        auto d = std::unique_ptr<CudaDevice>(new CudaDevice());
        return d->init() ? std::move(d) : nullptr;
    }
    ~CudaDevice() override {
        backend_.reset();
        if (ctx_) destroy_(ctx_);
    }
    const char* name() const override { return "CUDA"; }
    flinch::gpu::Backend& backend() override { return *backend_; }
    void* alloc(size_t bytes) override {
        unsigned long long p = 0;
        return alloc_(&p, bytes) == 0 ? reinterpret_cast<void*>(p) : nullptr;
    }
    void release(void* mem) override { free_(reinterpret_cast<unsigned long long>(mem)); }
    bool upload(void* mem, const std::vector<float>& d) override {
        return htod_(reinterpret_cast<unsigned long long>(mem), d.data(), d.size() * 4) == 0;
    }
    bool download(void* mem, std::vector<float>& d) override {
        return sync_() == 0 && dtoh_(d.data(), reinterpret_cast<unsigned long long>(mem), d.size() * 4) == 0;
    }

private:
    bool init() {
        HMODULE dll = LoadLibraryA("nvcuda.dll");
        if (!dll) return false;
        int (*cu_init)(unsigned) = nullptr;
        int (*device_get)(int*, int) = nullptr;
        int (*ctx_create)(void**, unsigned, int) = nullptr;
        if (!load_fn(dll, cu_init, "cuInit") || !load_fn(dll, device_get, "cuDeviceGet") ||
            !load_fn(dll, ctx_create, "cuCtxCreate_v2") || !load_fn(dll, destroy_, "cuCtxDestroy_v2") ||
            !load_fn(dll, alloc_, "cuMemAlloc_v2") || !load_fn(dll, free_, "cuMemFree_v2") ||
            !load_fn(dll, htod_, "cuMemcpyHtoD_v2") || !load_fn(dll, dtoh_, "cuMemcpyDtoH_v2") ||
            !load_fn(dll, sync_, "cuCtxSynchronize"))
            return false;
        int dev = 0;
        if (cu_init(0) != 0 || device_get(&dev, 0) != 0 || ctx_create(&ctx_, 0, dev) != 0) return false;
        backend_ = flinch::gpu::make_cuda(ctx_, nullptr);
        return backend_ != nullptr;
    }

    void* ctx_ = nullptr;
    std::unique_ptr<flinch::gpu::Backend> backend_;
    int (*destroy_)(void*) = nullptr;
    int (*alloc_)(unsigned long long*, size_t) = nullptr;
    int (*free_)(unsigned long long) = nullptr;
    int (*htod_)(unsigned long long, const void*, size_t) = nullptr;
    int (*dtoh_)(void*, unsigned long long, size_t) = nullptr;
    int (*sync_)() = nullptr;
};

// ---------------------------------------------------------------------------
// OpenCL
// ---------------------------------------------------------------------------

class OpenCLDevice final : public Device {
public:
    static std::unique_ptr<OpenCLDevice> create() {
        auto d = std::unique_ptr<OpenCLDevice>(new OpenCLDevice());
        return d->init() ? std::move(d) : nullptr;
    }
    ~OpenCLDevice() override {
        backend_.reset();
        if (queue_) release_queue_(queue_);
        if (ctx_) release_ctx_(ctx_);
    }
    const char* name() const override { return label_.c_str(); }
    flinch::gpu::Backend& backend() override { return *backend_; }
    void* alloc(size_t bytes) override {
        int err = 0;
        void* m = create_buffer_(ctx_, 1 /* CL_MEM_READ_WRITE */, bytes, nullptr, &err);
        return err == 0 ? m : nullptr;
    }
    void release(void* mem) override { release_mem_(mem); }
    bool upload(void* mem, const std::vector<float>& d) override {
        return write_(queue_, mem, 1, 0, d.size() * 4, d.data(), 0, nullptr, nullptr) == 0;
    }
    bool download(void* mem, std::vector<float>& d) override {
        return read_(queue_, mem, 1, 0, d.size() * 4, d.data(), 0, nullptr, nullptr) == 0;
    }

private:
    bool init() {
        HMODULE dll = LoadLibraryA("OpenCL.dll");
        if (!dll) return false;
        int(__stdcall * get_platforms)(unsigned, void**, unsigned*) = nullptr;
        int(__stdcall * get_devices)(void*, unsigned long long, unsigned, void**, unsigned*) = nullptr;
        int(__stdcall * get_platform_info)(void*, unsigned, size_t, void*, size_t*) = nullptr;
        void*(__stdcall * create_ctx)(const void*, unsigned, void**, void*, void*, int*) = nullptr;
        void*(__stdcall * create_queue)(void*, void*, unsigned long long, int*) = nullptr;
        if (!load_fn(dll, get_platforms, "clGetPlatformIDs") || !load_fn(dll, get_devices, "clGetDeviceIDs") ||
            !load_fn(dll, get_platform_info, "clGetPlatformInfo") ||
            !load_fn(dll, create_ctx, "clCreateContext") || !load_fn(dll, create_queue, "clCreateCommandQueue") ||
            !load_fn(dll, create_buffer_, "clCreateBuffer") || !load_fn(dll, write_, "clEnqueueWriteBuffer") ||
            !load_fn(dll, read_, "clEnqueueReadBuffer") || !load_fn(dll, release_mem_, "clReleaseMemObject") ||
            !load_fn(dll, release_queue_, "clReleaseCommandQueue") || !load_fn(dll, release_ctx_, "clReleaseContext"))
            return false;
        void* platforms[8] = {};
        unsigned count = 0;
        if (get_platforms(8, platforms, &count) != 0) return false;
        for (unsigned i = 0; i < count && !ctx_; ++i) {
            void* device = nullptr;
            unsigned n = 0;
            if (get_devices(platforms[i], 1ull << 2 /* CL_DEVICE_TYPE_GPU */, 1, &device, &n) != 0 || n == 0)
                continue;
            int err = 0;
            ctx_ = create_ctx(nullptr, 1, &device, nullptr, nullptr, &err);
            if (err != 0) { ctx_ = nullptr; continue; }
            queue_ = create_queue(ctx_, device, 0, &err);
            char vendor[128] = {};
            get_platform_info(platforms[i], 0x0903 /* CL_PLATFORM_NAME */, sizeof(vendor), vendor, nullptr);
            label_ = std::string("OpenCL (") + vendor + ")";
            backend_ = flinch::gpu::make_opencl(ctx_, device, queue_);
        }
        return backend_ != nullptr;
    }

    void* ctx_ = nullptr;
    void* queue_ = nullptr;
    std::string label_ = "OpenCL";
    std::unique_ptr<flinch::gpu::Backend> backend_;
    void*(__stdcall* create_buffer_)(void*, unsigned long long, size_t, void*, int*) = nullptr;
    int(__stdcall* write_)(void*, void*, unsigned, size_t, size_t, const void*, unsigned, const void*, void*) = nullptr;
    int(__stdcall* read_)(void*, void*, unsigned, size_t, size_t, void*, unsigned, const void*, void*) = nullptr;
    int(__stdcall* release_mem_)(void*) = nullptr;
    int(__stdcall* release_queue_)(void*) = nullptr;
    int(__stdcall* release_ctx_)(void*) = nullptr;
};

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

struct Scenario {
    const char* name;
    bool exact;  // must match the CPU almost exactly
    bool show_twitch_only;
    std::function<void(flinch::Params&)> setup;
};

flinch::Image test_image(int w, int h) {
    flinch::Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool check = ((x / 16) + (y / 16)) % 2 == 0;
            const float a = (x > w / 8 && x < w * 7 / 8) ? 1.0f : 0.0f;  // transparent edges
            img.at(x, y) = {a * float(x) / w, a * (check ? 0.9f : 0.2f), a * float(y) / h, a};
        }
    return img;
}

bool run(Device& dev, const Scenario& sc) {
    const int w = 320, h = 180, frames = 45;
    const flinch::Image src = test_image(w, h);
    const std::vector<float> src_bgra = to_bgra(src);

    flinch::Params p;
    p.speed = 8;
    p.blur.enabled = p.color.enabled = p.light.enabled = p.scale.enabled = p.slide.enabled = false;
    p.time.enabled = false;
    sc.setup(p);

    void* in_mem = dev.alloc(src_bgra.size() * 4);
    void* out_mem = dev.alloc(src_bgra.size() * 4);
    if (!in_mem || !out_mem || !dev.upload(in_mem, src_bgra)) {
        std::printf("  %-28s alloc/upload failed\n", sc.name);
        return false;
    }

    double max_diff = 0, sum_diff = 0;
    long long samples = 0;
    int active = 0;
    std::vector<float> out_bgra(src_bgra.size());
    for (int f = 0; f < frames; ++f) {
        const flinch::FrameState st = flinch::evaluate(p, f, 30, w, h);
        active += st.any_active();

        flinch::Image cpu(w, h);
        if (!st.any_active()) {
            if (!sc.show_twitch_only) cpu = src;
        } else {
            flinch::render_frame(src, cpu, st, {}, flinch::Border::Mirror);
        }

        flinch::gpu::Layout layout;
        layout.canvas_w = w;
        layout.canvas_h = h;
        layout.border = int(flinch::Border::Mirror);
        layout.show_twitch_only = sc.show_twitch_only;
        DeviceScratch scratch(dev);
        if (!flinch::gpu::render(dev.backend(), scratch, st, layout, {in_mem, w, h, w}, {out_mem, w, h, w}) ||
            !dev.download(out_mem, out_bgra)) {
            std::printf("  %-28s GPU render failed at frame %d\n", sc.name, f);
            return false;
        }
        const flinch::Image gpu = from_bgra(out_bgra, w, h);
        for (size_t i = 0; i < cpu.px.size(); ++i) {
            const float d[4] = {cpu.px[i].r - gpu.px[i].r, cpu.px[i].g - gpu.px[i].g,
                                cpu.px[i].b - gpu.px[i].b, cpu.px[i].a - gpu.px[i].a};
            for (float v : d) {
                max_diff = std::max<double>(max_diff, std::abs(v));
                sum_diff += std::abs(v);
                ++samples;
            }
        }
    }
    dev.release(in_mem);
    dev.release(out_mem);

    const double mean = sum_diff / double(samples);
    const bool ok = sc.exact ? max_diff < 2e-3 : mean < 0.02;
    std::printf("  %-28s %s  max %.5f  mean %.5f  (%d/%d frames twitching)\n", sc.name, ok ? "PASS" : "FAIL",
                max_diff, mean, active, frames);
    return ok;
}

}  // namespace

int main() {
    const Scenario scenarios[] = {
        {"idle copy", true, false, [](flinch::Params& p) { p.speed = 0; }},
        {"show twitch only (idle)", true, true, [](flinch::Params& p) { p.speed = 0; }},
        {"slide + scale, no blur", true, false,
         [](flinch::Params& p) {
             p.slide.enabled = p.scale.enabled = true;
             p.slide_motion_blur = 0;
         }},
        {"blur + light + color", true, false,
         [](flinch::Params& p) {
             p.blur.enabled = p.light.enabled = p.color.enabled = true;
             p.blur.amount = 40;
             p.light_mode = flinch::LightMode::Both;
         }},
        {"all operators, no motion", true, false,
         [](flinch::Params& p) {
             p.blur.enabled = p.light.enabled = p.color.enabled = p.scale.enabled = p.slide.enabled = true;
             p.slide_motion_blur = 0;
             p.slide_spread = 0.3;
             p.slide_direction_deg = 30;
         }},
        {"slide motion blur", false, false, [](flinch::Params& p) { p.slide.enabled = true; }},
        {"slide RGB split + motion", false, false,
         [](flinch::Params& p) {
             p.slide.enabled = true;
             p.slide_rgb_split = 80;
         }},
        {"scale motion blur", false, false,
         [](flinch::Params& p) {
             p.scale.enabled = true;
             p.scale.amount = 80;
             p.scale_motion_blur = 100;
         }},
    };

    bool all_ok = true, any_device = false;
    std::unique_ptr<Device> devices[] = {CudaDevice::create(), OpenCLDevice::create()};
    for (auto& dev : devices) {
        if (!dev) continue;
        any_device = true;
        std::printf("%s\n", dev->name());
        for (const Scenario& sc : scenarios) all_ok &= run(*dev, sc);
    }
    if (!any_device) {
        std::printf("No CUDA or OpenCL GPU available.\n");
        return 2;
    }
    std::printf(all_ok ? "All GPU checks passed\n" : "Some GPU checks failed\n");
    return all_ok ? 0 : 1;
}
