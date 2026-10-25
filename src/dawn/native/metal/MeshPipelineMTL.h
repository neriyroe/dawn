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

#ifndef SRC_DAWN_NATIVE_METAL_MESHPIPELINEMTL_H_
#define SRC_DAWN_NATIVE_METAL_MESHPIPELINEMTL_H_

#import <Metal/Metal.h>

#include <vector>

#include "src/dawn/common/NSRef.h"
#include "src/dawn/native/BindingInfo.h"
#include "src/dawn/native/Error.h"
#include "src/dawn/native/PerStage.h"

namespace dawn::native::metal {

class BindGroup;
class RenderPipeline;

// Owns native object/mesh stage state and compiler-assigned resource bindings.
class MeshPipeline {
  public:
    ResultOrError<NSPRef<id<MTLRenderPipelineState>>> Initialize(
        RenderPipeline* pipeline,
        MTLRenderPipelineDescriptor* rasterDescriptor);
    void ApplyBindings(id<MTLRenderCommandEncoder> encoder,
                       BindGroupIndex groupIndex,
                       BindGroup* group,
                       ityp::span<BindingIndex, uint32_t> dynamicOffsets) const;
    void Draw(id<MTLRenderCommandEncoder> encoder, MTLSize groupCount) const;
    uint64_t GetMaxTaskGroups() const;

  private:
    struct BufferSlot {
        BindingIndex binding;
        uint32_t metalIndex;
        bool dynamic;
    };
    PerStage<PerBindGroup<std::vector<BufferSlot>>> mBindings;
    MTLSize mObjectWorkgroupSize = {0, 0, 0};
    MTLSize mMeshWorkgroupSize = {0, 0, 0};
    uint64_t mMaxTaskGroups = 1024;
};

}  // namespace dawn::native::metal

#endif  // SRC_DAWN_NATIVE_METAL_MESHPIPELINEMTL_H_
