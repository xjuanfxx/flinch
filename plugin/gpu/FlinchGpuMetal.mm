// FlinchGpuMetal.mm — Metal backend (macOS, Apple Silicon and Intel).
//
// The kernels are compiled from source when the device is set up. Built with ARC.
#include "FlinchGpu.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <initializer_list>

#include "flinch_kernels_metal.h"  // generated: kFlinchMetalSource

namespace flinch::gpu {
namespace {

class MetalBackend final : public Backend {
public:
    MetalBackend(id<MTLDevice> device, id<MTLCommandQueue> queue) : device_(device), queue_(queue) {}

    bool init() {
        @autoreleasepool {
            NSError* error = nil;
            NSString* source = [NSString stringWithUTF8String:kFlinchMetalSource];
            id<MTLLibrary> library = [device_ newLibraryWithSource:source options:nil error:&error];
            if (!library) return false;
            geometry_ = pipeline(library, @"flinch_geometry");
            blur_ = pipeline(library, @"flinch_blur");
            finish_ = pipeline(library, @"flinch_finish");
            return geometry_ && blur_ && finish_;
        }
    }

    bool geometry(void* src, void* dst, const FlinchGeoParams& p) override {
        return run(geometry_, {src, dst}, &p, sizeof(p), p.out_w, p.out_h);
    }
    bool blur(void* src, void* dst, const FlinchBlurParams& p) override {
        return run(blur_, {src, dst}, &p, sizeof(p), p.w, p.h);
    }
    bool finish(void* geo, void* blurred, void* dst, const FlinchFinishParams& p) override {
        return run(finish_, {geo, blurred, dst}, &p, sizeof(p), p.w, p.h);
    }

private:
    id<MTLComputePipelineState> pipeline(id<MTLLibrary> library, NSString* name) {
        id<MTLFunction> fn = [library newFunctionWithName:name];
        if (!fn) return nil;
        NSError* error = nil;
        return [device_ newComputePipelineStateWithFunction:fn error:&error];
    }

    bool run(id<MTLComputePipelineState> state, std::initializer_list<void*> buffers, const void* params,
             size_t params_size, int w, int h) {
        @autoreleasepool {
            id<MTLCommandBuffer> commands = [queue_ commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:state];
            NSUInteger index = 0;
            for (void* buffer : buffers) {
                [encoder setBuffer:(__bridge id<MTLBuffer>)buffer offset:0 atIndex:index++];
            }
            [encoder setBytes:params length:params_size atIndex:index];
            const NSUInteger tw = state.threadExecutionWidth;
            const NSUInteger th = std::max<NSUInteger>(1, state.maxTotalThreadsPerThreadgroup / tw);
            const MTLSize group = MTLSizeMake(tw, th, 1);
            const MTLSize groups = MTLSizeMake((NSUInteger(w) + tw - 1) / tw, (NSUInteger(h) + th - 1) / th, 1);
            [encoder dispatchThreadgroups:groups threadsPerThreadgroup:group];
            [encoder endEncoding];
            [commands commit];
            return commands.error == nil;
        }
    }

    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLComputePipelineState> geometry_ = nil, blur_ = nil, finish_ = nil;
};

}  // namespace

std::unique_ptr<Backend> make_metal(void* device, void* queue) {
    if (!device || !queue) return nullptr;
    auto backend = std::make_unique<MetalBackend>((__bridge id<MTLDevice>)device, (__bridge id<MTLCommandQueue>)queue);
    if (!backend->init()) return nullptr;
    return backend;
}

}  // namespace flinch::gpu
