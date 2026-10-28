// FlinchGpuMetalStub.cpp — Metal only exists on macOS (see FlinchGpuMetal.mm).
#include "FlinchGpu.h"

namespace flinch::gpu {
std::unique_ptr<Backend> make_metal(void*, void*) { return nullptr; }
}  // namespace flinch::gpu
