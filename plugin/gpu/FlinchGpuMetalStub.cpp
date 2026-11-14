// FlinchGpuMetalStub.cpp — Metal only exists on macOS (see FlinchGpuMetal.mm).
#include "FlinchGpu.h"

namespace flinch::gpu {
std::unique_ptr<Backend> make_metal(void*, void*, std::string& error) {
    error = "Metal is only available on macOS";
    return nullptr;
}
}  // namespace flinch::gpu
