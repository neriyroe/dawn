// Copyright 2019 The Dawn & Tint Authors
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

#include <cstring>
#include <optional>
#include <vector>

#include "src/dawn/native/d3d12/BufferD3D12.h"
#include "src/dawn/native/d3d12/CommandRecordingContext.h"
#include "src/dawn/native/d3d12/DeviceD3D12.h"
#include "src/dawn/native/d3d12/QueueD3D12.h"
#include "src/dawn/native/d3d12/ResidencyManagerD3D12.h"
#include "src/dawn/native/d3d12/TextureD3D12.h"
#include "src/dawn/tests/DawnTest.h"

namespace dawn::native::d3d12 {
namespace {

class D3D12ResourceHeapTests : public DawnTest {
  protected:
    void SetUp() override {
        DawnTest::SetUp();
        DAWN_TEST_UNSUPPORTED_IF(UsesWire());
    }

    std::vector<wgpu::FeatureName> GetRequiredFeatures() override {
        mIsBCFormatSupported = SupportsFeatures({wgpu::FeatureName::TextureCompressionBC});
        if (!mIsBCFormatSupported) {
            return {};
        }

        return {wgpu::FeatureName::TextureCompressionBC};
    }

    bool IsBCFormatSupported() const { return mIsBCFormatSupported; }

  private:
    bool mIsBCFormatSupported = false;
};

// Verify that creating a small compressed texture will be 4KB aligned.
TEST_P(D3D12ResourceHeapTests, AlignSmallCompressedTexture) {
    DAWN_TEST_UNSUPPORTED_IF(!IsBCFormatSupported());

    wgpu::TextureDescriptor descriptor;
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size.width = 8;
    descriptor.size.height = 8;
    descriptor.size.depthOrArrayLayers = 1;
    descriptor.sampleCount = 1;
    descriptor.format = wgpu::TextureFormat::BC1RGBAUnorm;
    descriptor.mipLevelCount = 1;
    descriptor.usage = wgpu::TextureUsage::TextureBinding;

    // Create a smaller one that allows use of the smaller alignment.
    wgpu::Texture texture = device.CreateTexture(&descriptor);
    Texture* d3dTexture = reinterpret_cast<Texture*>(texture.Get());

    EXPECT_EQ(d3dTexture->GetD3D12Resource()->GetDesc().Alignment,
              static_cast<uint64_t>(D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT));

    // Create a larger one (>64KB) that forbids use the smaller alignment.
    descriptor.size.width = 4096;
    descriptor.size.height = 4096;

    texture = device.CreateTexture(&descriptor);
    d3dTexture = reinterpret_cast<Texture*>(texture.Get());

    EXPECT_EQ(d3dTexture->GetD3D12Resource()->GetDesc().Alignment,
              static_cast<uint64_t>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
}

// Verify creating a UBO will always be 256B aligned.
TEST_P(D3D12ResourceHeapTests, AlignUBO) {
    // Create a small UBO
    wgpu::BufferDescriptor descriptor;
    descriptor.size = 4ULL * 1024;
    descriptor.usage = wgpu::BufferUsage::Uniform;

    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);
    Buffer* d3dBuffer = reinterpret_cast<Buffer*>(buffer.Get());

    EXPECT_EQ((d3dBuffer->GetD3D12Resource()->GetDesc().Width %
               static_cast<uint64_t>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)),
              0u);

    // Create a larger UBO
    descriptor.size = (4 * 1024 * 1024) + 255;
    descriptor.usage = wgpu::BufferUsage::Uniform;

    buffer = device.CreateBuffer(&descriptor);
    d3dBuffer = reinterpret_cast<Buffer*>(buffer.Get());

    EXPECT_EQ((d3dBuffer->GetD3D12Resource()->GetDesc().Width %
               static_cast<uint64_t>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)),
              0u);
}

// Verify the MSAA textures are 64KB alignment if the alignment is supported for MSAA textures on
// the current platform. Otherwise it will be 4MB.
TEST_P(D3D12ResourceHeapTests, MSAATexture) {
    wgpu::TextureDescriptor descriptor;
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size.width = 8;
    descriptor.size.height = 8;
    descriptor.size.depthOrArrayLayers = 1;
    descriptor.sampleCount = 4;
    descriptor.format = wgpu::TextureFormat::RGBA16Float;
    descriptor.mipLevelCount = 1;
    descriptor.usage = wgpu::TextureUsage::RenderAttachment;

    // Create a smaller MSAA texture
    wgpu::Texture texture = device.CreateTexture(&descriptor);
    Texture* d3dTexture = reinterpret_cast<Texture*>(texture.Get());

    uint64_t expectedAlignment = HasToggleEnabled("d3d12_use_64kb_alignment_msaa_texture")
                                     ? D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
                                     : D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
    EXPECT_EQ(d3dTexture->GetD3D12Resource()->GetDesc().Alignment, expectedAlignment);

    // Create a larger MSAA texture. Currently it will be created as a committed resource in Dawn.
    descriptor.size.width = 4096;
    descriptor.size.height = 4096;

    texture = device.CreateTexture(&descriptor);
    d3dTexture = reinterpret_cast<Texture*>(texture.Get());

    EXPECT_EQ(d3dTexture->GetD3D12Resource()->GetDesc().Alignment, expectedAlignment);
}

DAWN_INSTANTIATE_TEST(D3D12ResourceHeapTests,
                      D3D12Backend(),
                      D3D12Backend({}, {"d3d12_use_64kb_alignment_msaa_texture"}));

class D3D12GPUUploadHeapTests : public DawnTest {
  protected:
    void SetUp() override {
        DawnTest::SetUp();
        DAWN_TEST_UNSUPPORTED_IF(UsesWire());
    }

    D3D12_HEAP_TYPE HeapType(const wgpu::Buffer& buffer) {
        D3D12_HEAP_PROPERTIES properties = {};
        EXPECT_HRESULT_SUCCEEDED(reinterpret_cast<Buffer*>(buffer.Get())
                                     ->GetD3D12Resource()
                                     ->GetHeapProperties(&properties, nullptr));
        return properties.Type;
    }
};

TEST_P(D3D12GPUUploadHeapTests, UploadAndReadback) {
    wgpu::BufferDescriptor uploadDescriptor;
    uploadDescriptor.size = sizeof(uint32_t);
    uploadDescriptor.usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;
    uploadDescriptor.mappedAtCreation = true;
    wgpu::Buffer upload = device.CreateBuffer(&uploadDescriptor);

    D3D12_HEAP_TYPE expectedUpload = D3D12_HEAP_TYPE_UPLOAD;
#if D3D12_SDK_VERSION >= 613
    if (UseGPUUploadHeap(device.Get()) &&
        HasToggleEnabled("d3d12_use_gpu_upload_heap_for_staging")) {
        expectedUpload = D3D12_HEAP_TYPE_GPU_UPLOAD;
    }
#endif
    EXPECT_EQ(HeapType(upload), expectedUpload);
    const uint32_t value = 0x12345678;
    std::memcpy(upload.GetMappedRange(), &value, sizeof(value));
    upload.Unmap();

    wgpu::BufferDescriptor readbackDescriptor;
    readbackDescriptor.size = sizeof(value);
    readbackDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer readback = device.CreateBuffer(&readbackDescriptor);
    EXPECT_EQ(HeapType(readback), D3D12_HEAP_TYPE_READBACK);

    auto encoder = device.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(upload, 0, readback, 0, sizeof(value));
    auto commands = encoder.Finish();
    queue.Submit(1, &commands);

    bool done = false;
    readback.MapAsync(wgpu::MapMode::Read, 0, sizeof(value),
                      wgpu::CallbackMode::AllowProcessEvents,
                      [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                          EXPECT_EQ(status, wgpu::MapAsyncStatus::Success);
                          done = true;
                      });
    while (!done) {
        WaitABit();
    }
    ASSERT_NE(readback.GetConstMappedRange(), nullptr);
    EXPECT_EQ(*static_cast<const uint32_t*>(readback.GetConstMappedRange()), value);
    readback.Unmap();
}

TEST_P(D3D12GPUUploadHeapTests, LocalBudgetFallsBackToUpload) {
    DAWN_TEST_UNSUPPORTED_IF(!UseGPUUploadHeap(device.Get()) ||
                             !HasToggleEnabled("d3d12_use_gpu_upload_heap_for_staging"));
    reinterpret_cast<Device*>(device.Get())->GetResidencyManager()->RestrictBudgetForTesting(0);

    wgpu::BufferDescriptor descriptor;
    descriptor.size = 8 * 1024 * 1024;
    descriptor.usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;
    descriptor.mappedAtCreation = true;
    wgpu::Buffer upload = device.CreateBuffer(&descriptor);
    EXPECT_EQ(HeapType(upload), D3D12_HEAP_TYPE_UPLOAD);
    EXPECT_NE(upload.GetMappedRange(), nullptr);
    upload.Unmap();
}

TEST_P(D3D12GPUUploadHeapTests, GPUWrittenBuffersUseDefaultHeap) {
    wgpu::BufferDescriptor descriptor;
    descriptor.size = 256;
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage;
    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);
    EXPECT_EQ(HeapType(buffer), D3D12_HEAP_TYPE_DEFAULT);
}

TEST_P(D3D12GPUUploadHeapTests, CPUWrittenBuffersUseGPUUploadHeap) {
    wgpu::BufferDescriptor descriptor;
    descriptor.size = 256;
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);

    D3D12_HEAP_TYPE expected = D3D12_HEAP_TYPE_DEFAULT;
#if D3D12_SDK_VERSION >= 613
    if (UseGPUUploadHeap(device.Get())) {
        expected = D3D12_HEAP_TYPE_GPU_UPLOAD;
    }
#endif
    EXPECT_EQ(HeapType(buffer), expected);
}

// The first write stages; later writes of an idle buffer go straight into it. Both must land, and a
// partial write must leave the rest of the buffer alone.
TEST_P(D3D12GPUUploadHeapTests, RepeatedWritesLand) {
    wgpu::BufferDescriptor descriptor;
    descriptor.size = 4 * sizeof(uint32_t);
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);

    const uint32_t first[] = {1u, 2u, 3u, 4u};
    queue.WriteBuffer(buffer, 0, first, sizeof(first));
    EXPECT_BUFFER_U32_RANGE_EQ(first, buffer, 0, 4);

    WaitForAllOperations();
    const uint32_t second[] = {5u, 6u, 7u, 8u};
    queue.WriteBuffer(buffer, 0, second, sizeof(second));
    EXPECT_BUFFER_U32_RANGE_EQ(second, buffer, 0, 4);

    WaitForAllOperations();
    const uint32_t tail[] = {9u, 10u};
    queue.WriteBuffer(buffer, 2 * sizeof(uint32_t), tail, sizeof(tail));
    const uint32_t merged[] = {5u, 6u, 9u, 10u};
    EXPECT_BUFFER_U32_RANGE_EQ(merged, buffer, 0, 4);
}

// A buffer whose very first write is partial owes zeroes everywhere the write does not reach.
TEST_P(D3D12GPUUploadHeapTests, PartialWriteZeroFillsTheRest) {
    wgpu::BufferDescriptor descriptor;
    descriptor.size = 4 * sizeof(uint32_t);
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);

    const uint32_t head[] = {0xAAAAAAAAu};
    queue.WriteBuffer(buffer, 0, head, sizeof(head));
    WaitForAllOperations();
    const uint32_t again[] = {0xBBBBBBBBu};
    queue.WriteBuffer(buffer, sizeof(uint32_t), again, sizeof(again));

    const uint32_t expected[] = {0xAAAAAAAAu, 0xBBBBBBBBu, 0u, 0u};
    EXPECT_BUFFER_U32_RANGE_EQ(expected, buffer, 0, 4);
}

DAWN_INSTANTIATE_TEST(D3D12GPUUploadHeapTests,
                      D3D12Backend(),
                      D3D12Backend({"d3d12_use_gpu_upload_heap",
                                    "d3d12_use_gpu_upload_heap_for_staging"}),
                      D3D12Backend({"d3d12_use_gpu_upload_heap_for_staging"},
                                    {"d3d12_use_gpu_upload_heap"}),
                      D3D12Backend({"d3d12_use_gpu_upload_heap",
                                    "d3d12_use_gpu_upload_heap_for_staging",
                                    "disable_resource_suballocation"}));

class D3D12BufferBarrierTests : public DawnTest {
  protected:
    void SetUp() override {
        DawnTest::SetUp();
        DAWN_TEST_UNSUPPORTED_IF(UsesWire());

        wgpu::BufferDescriptor descriptor;
        descriptor.size = sizeof(kValue);
        descriptor.usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;
        descriptor.mappedAtCreation = true;
        mUpload = device.CreateBuffer(&descriptor);
        std::memcpy(mUpload.GetMappedRange(), &kValue, sizeof(kValue));
        mUpload.Unmap();

        descriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
        descriptor.mappedAtCreation = false;
        mReadback = device.CreateBuffer(&descriptor);
    }

    wgpu::Buffer CreateBuffer() {
        wgpu::BufferDescriptor descriptor;
        descriptor.size = sizeof(kValue);
        descriptor.usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst |
                           wgpu::BufferUsage::Storage;
        return device.CreateBuffer(&descriptor);
    }

    std::optional<D3D12_RESOURCE_BARRIER> TrackUsage(const wgpu::Buffer& buffer,
                                                   wgpu::BufferUsage usage) {
        auto* d3dDevice = reinterpret_cast<Device*>(device.Get());
        auto guard = d3dDevice->GetGuard();
        auto* d3dBuffer = reinterpret_cast<Buffer*>(buffer.Get());
        auto scopedUse = d3dBuffer->UseInternal();
        auto* context = ToBackend(d3dDevice->GetQueue())->GetPendingCommandContext();
        auto* commandList = context->GetCommandList();

        D3D12_RESOURCE_BARRIER barrier = {};
        std::optional<D3D12_RESOURCE_BARRIER> result;
        if (d3dBuffer->TrackUsageAndGetResourceBarrier(context, &barrier, usage)) {
            commandList->ResourceBarrier(1, &barrier);
            result = barrier;
        }

        if (usage == wgpu::BufferUsage::CopyDst || usage == wgpu::BufferUsage::CopySrc) {
            const bool isWrite = usage == wgpu::BufferUsage::CopyDst;
            auto* fixedBuffer = reinterpret_cast<Buffer*>((isWrite ? mUpload : mReadback).Get());
            auto scopedFixedUse = fixedBuffer->UseInternal();
            EXPECT_FALSE(fixedBuffer->TrackUsageAndGetResourceBarrier(
                context, &barrier,
                isWrite ? wgpu::BufferUsage::CopySrc : wgpu::BufferUsage::CopyDst));
            auto* source = isWrite ? fixedBuffer : d3dBuffer;
            auto* destination = isWrite ? d3dBuffer : fixedBuffer;
            commandList->CopyBufferRegion(destination->GetD3D12Resource(), 0,
                                          source->GetD3D12Resource(), 0, sizeof(kValue));
            destination->SetInitialized(true);
        }
        return result;
    }

    void CheckReadback() {
        MapAsyncAndWait(mReadback, wgpu::MapMode::Read, 0, sizeof(kValue));
        ASSERT_NE(mReadback.GetConstMappedRange(), nullptr);
        EXPECT_EQ(*static_cast<const uint32_t*>(mReadback.GetConstMappedRange()), kValue);
        mReadback.Unmap();
    }

    static constexpr uint32_t kValue = 0x12345678;
    wgpu::Buffer mUpload;
    wgpu::Buffer mReadback;
};

TEST_P(D3D12BufferBarrierTests, BuffersDecayBetweenSubmissions) {
    wgpu::Buffer buffer = CreateBuffer();
    EXPECT_FALSE(TrackUsage(buffer, wgpu::BufferUsage::CopyDst).has_value());
    queue.Submit(0, nullptr);

    EXPECT_FALSE(TrackUsage(buffer, wgpu::BufferUsage::CopySrc).has_value());
    queue.Submit(0, nullptr);
    CheckReadback();
}

TEST_P(D3D12BufferBarrierTests, UnchangedFirstUsagePreservesSameSubmitTransition) {
    wgpu::Buffer buffer = CreateBuffer();
    EXPECT_FALSE(TrackUsage(buffer, wgpu::BufferUsage::CopyDst).has_value());
    queue.Submit(0, nullptr);

    EXPECT_FALSE(TrackUsage(buffer, wgpu::BufferUsage::CopyDst).has_value());
    auto barrier = TrackUsage(buffer, wgpu::BufferUsage::CopySrc);
    ASSERT_TRUE(barrier.has_value());
    EXPECT_EQ(barrier->Type, D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
    EXPECT_EQ(barrier->Transition.StateBefore, D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_EQ(barrier->Transition.StateAfter, D3D12_RESOURCE_STATE_COPY_SOURCE);
    queue.Submit(0, nullptr);
    CheckReadback();
}

TEST_P(D3D12BufferBarrierTests, UAVBarriersOnlyWithinSubmission) {
    wgpu::Buffer buffer = CreateBuffer();
    for (uint32_t submission = 0; submission < 2; ++submission) {
        EXPECT_FALSE(TrackUsage(buffer, wgpu::BufferUsage::Storage).has_value());
        auto barrier = TrackUsage(buffer, wgpu::BufferUsage::Storage);
        ASSERT_TRUE(barrier.has_value());
        EXPECT_EQ(barrier->Type, D3D12_RESOURCE_BARRIER_TYPE_UAV);
        EXPECT_EQ(barrier->UAV.pResource, reinterpret_cast<Buffer*>(buffer.Get())->GetD3D12Resource());
        queue.Submit(0, nullptr);
    }
}

DAWN_INSTANTIATE_TEST(D3D12BufferBarrierTests, D3D12Backend());

}  // anonymous namespace
}  // namespace dawn::native::d3d12
