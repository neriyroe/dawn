// Copyright 2017 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/dawn/native/metal/MeshPipelineMTL.h"

#include <bitset>

#include "src/dawn/native/metal/BindGroupMTL.h"
#include "src/dawn/native/metal/BufferMTL.h"
#include "src/dawn/native/metal/DeviceMTL.h"
#include "src/dawn/native/metal/PipelineLayoutMTL.h"
#include "src/dawn/native/metal/RenderPipelineMTL.h"
#include "src/dawn/native/metal/ShaderModuleMTL.h"

namespace dawn::native::metal {

ResultOrError<NSPRef<id<MTLRenderPipelineState>>> MeshPipeline::Initialize(
    RenderPipeline* pipeline,
    MTLRenderPipelineDescriptor* rasterDescriptor) {
    if (@available(macOS 26.0, iOS 26.0, tvOS 26.0, *)) {
        auto descriptor = AcquireNSRef([MTLMeshRenderPipelineDescriptor new]);
        descriptor.Get().label = rasterDescriptor.label;
        descriptor.Get().fragmentFunction = rasterDescriptor.fragmentFunction;
        descriptor.Get().rasterSampleCount = rasterDescriptor.rasterSampleCount;
        descriptor.Get().alphaToCoverageEnabled = rasterDescriptor.alphaToCoverageEnabled;
        descriptor.Get().alphaToOneEnabled = rasterDescriptor.alphaToOneEnabled;
        descriptor.Get().rasterizationEnabled = rasterDescriptor.rasterizationEnabled;
        descriptor.Get().depthAttachmentPixelFormat = rasterDescriptor.depthAttachmentPixelFormat;
        descriptor.Get().stencilAttachmentPixelFormat =
            rasterDescriptor.stencilAttachmentPixelFormat;
        for (NSUInteger index = 0; index < kMaxColorAttachments; ++index) {
            descriptor.Get().colorAttachments[index] = rasterDescriptor.colorAttachments[index];
        }

        // Metal derives the payload allocation from the compiled object_data layout.
        descriptor.Get().payloadMemoryLength = 0;

        auto* layout = ToBackend(pipeline->GetLayout());
        const auto& stages = pipeline->GetAllStages();
        for (auto stage : IterateStages(pipeline->GetStageMask() &
                                        (wgpu::ShaderStage::Task | wgpu::ShaderStage::Mesh))) {
            const auto& programmable = stages[stage];
            auto* module = ToBackend(programmable.module.Get());
            ShaderModule::MetalFunctionData function;
            DAWN_TRY(module->CreateFunction(stage, programmable, layout,
                                            pipeline->GetImmediateMask(), &function));
            const auto size = function.localWorkgroupSize;
            const uint64_t threads = uint64_t{size.width} * size.height * size.depth;
            DAWN_INVALID_IF(size.width == 0 || size.height == 0 || size.depth == 0 ||
                                size.width > 256 || size.height > 256 || size.depth > 256 ||
                                threads > 256,
                            "Native Metal workgroup dimensions exceed 256 threads.");

            if (stage == SingleShaderStage::Task) {
                mObjectWorkgroupSize = size;
                descriptor.Get().objectFunction = function.function.Get();
                descriptor.Get().maxTotalThreadsPerObjectThreadgroup = threads;
            } else {
                mMeshWorkgroupSize = size;
                descriptor.Get().meshFunction = function.function.Get();
                descriptor.Get().maxTotalThreadsPerMeshThreadgroup = threads;
            }

            std::bitset<kMetalBufferTableSize> occupied;
            for (const auto& binding : module->GetNativeBindings()) {
                if (binding.metalIndex == WGPU_LIMIT_U32_UNDEFINED) {
                    continue;
                }
                DAWN_INVALID_IF(
                    binding.metalIndex >= kMetalBufferTableSize || occupied[binding.metalIndex],
                    "Native Metal buffer index (%u) is invalid or duplicated.", binding.metalIndex);
                occupied.set(binding.metalIndex);
                const auto group = BindGroupIndex{binding.group};
                auto* groupLayout = layout->GetBindGroupLayout(group);
                auto index = groupLayout->AsBindingIndex(
                    groupLayout->GetAPIBindingIndex(BindingNumber{binding.binding}));
                const auto& info =
                    std::get<BufferBindingInfo>(groupLayout->GetBindingInfo(index).bindingLayout);
                mBindings[stage][group].push_back(
                    {index, binding.metalIndex, info.hasDynamicOffset});
            }
        }

        auto device = ToBackend(pipeline->GetDevice())->GetMTLDevice();
        if (mObjectWorkgroupSize.width != 0) {
            mMaxTaskGroups = 1u << 22;
        } else if ([device supportsFamily:MTLGPUFamilyApple10]) {
            mMaxTaskGroups = 4194303;
        } else if ([device supportsFamily:MTLGPUFamilyApple9]) {
            mMaxTaskGroups = 1048575;
        }
        NSError* error = nullptr;
        auto state =
            AcquireNSPRef([device newRenderPipelineStateWithMeshDescriptor:descriptor.Get()
                                                                   options:MTLPipelineOptionNone
                                                                reflection:nil
                                                                     error:&error]);
        DAWN_INVALID_IF(state == nil, "Metal mesh pipeline creation failed: %s",
                        error ? [error.localizedDescription UTF8String] : "No pipeline state");
        DAWN_INVALID_IF(
            mMeshWorkgroupSize.width * mMeshWorkgroupSize.height * mMeshWorkgroupSize.depth >
                    state.Get().maxTotalThreadsPerMeshThreadgroup ||
                mObjectWorkgroupSize.width * mObjectWorkgroupSize.height *
                        mObjectWorkgroupSize.depth >
                    state.Get().maxTotalThreadsPerObjectThreadgroup,
            "Native Metal workgroup exceeds the compiled pipeline limit.");
        return state;
    }
    return DAWN_VALIDATION_ERROR("Metal mesh pipelines require Metal 4.");
}

void MeshPipeline::ApplyBindings(id<MTLRenderCommandEncoder> encoder,
                                 BindGroupIndex groupIndex,
                                 BindGroup* group,
                                 ityp::span<BindingIndex, uint32_t> dynamicOffsets) const {
    if (@available(macOS 26.0, iOS 26.0, tvOS 26.0, *)) {
        for (auto stage : {SingleShaderStage::Task, SingleShaderStage::Mesh}) {
            for (const auto& slot : mBindings[stage][groupIndex]) {
                const auto& binding = group->GetBindingAsBufferBinding(slot.binding);
                auto* buffer = ToBackend(binding.buffer);
                buffer->TrackUsage();
                const NSUInteger offset =
                    binding.offset + (slot.dynamic ? dynamicOffsets[slot.binding] : 0);
                if (stage == SingleShaderStage::Task) {
                    [encoder setObjectBuffer:buffer->GetMTLBuffer()
                                      offset:offset
                                     atIndex:slot.metalIndex];
                } else {
                    [encoder setMeshBuffer:buffer->GetMTLBuffer()
                                    offset:offset
                                   atIndex:slot.metalIndex];
                }
            }
        }
    }
}

void MeshPipeline::Draw(id<MTLRenderCommandEncoder> encoder, MTLSize groupCount) const {
    if (@available(macOS 26.0, iOS 26.0, tvOS 26.0, *)) {
        [encoder drawMeshThreadgroups:groupCount
            threadsPerObjectThreadgroup:mObjectWorkgroupSize
              threadsPerMeshThreadgroup:mMeshWorkgroupSize];
    }
}

uint64_t MeshPipeline::GetMaxTaskGroups() const {
    return mMaxTaskGroups;
}

}  // namespace dawn::native::metal
