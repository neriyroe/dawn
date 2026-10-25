// Copyright 2021 The Dawn & Tint Authors
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

#include <array>
#include <string>
#include <vector>

#include "src/dawn/tests/DawnTest.h"
#include "src/dawn/utils/ComboRenderPipelineDescriptor.h"
#include "src/dawn/utils/WGPUHelpers.h"

namespace dawn {
namespace {

class MetalMeshShaderTests : public DawnTest {
  protected:
    std::vector<wgpu::FeatureName> GetRequiredFeatures() override {
        return SupportsFeatures({wgpu::FeatureName::DawnMeshShader})
                   ? std::vector{wgpu::FeatureName::DawnMeshShader}
                   : std::vector<wgpu::FeatureName>{};
    }

    wgpu::ShaderModule NativeModule(wgpu::ShaderStage stage,
                                    const char* source,
                                    const wgpu::DawnNativeBinding& binding, uint32_t threads = 1) {
        wgpu::DawnShaderSourceNative native;
        native.stage = stage;
        native.entryPoint = "main";
        native.mslEntryPoint = "entry";
        native.msl = source;
        native.workgroupSize = {threads, 1, 1};
        native.bindingCount = 1;
        native.bindings = &binding;
        wgpu::ShaderModuleDescriptor descriptor;
        descriptor.nextInChain = &native;
        return device.CreateShaderModule(&descriptor);
    }

    void CheckCompactedPayload(bool bundle) {
        DAWN_TEST_UNSUPPORTED_IF(!device.HasFeature(wgpu::FeatureName::DawnMeshShader));
        const char* taskSource = R"(
            #include <metal_stdlib>
            using namespace metal;
            struct Payload { uint instance; array<uint, 32> meshlets; };
            [[object]] void entry(uint lane [[thread_index_in_threadgroup]],
                                 uint3 group [[threadgroup_position_in_grid]],
                                 object_data Payload* output [[payload]], mesh_grid_properties grid) {
                threadgroup Payload payload;
                bool visible = (lane + group.x) % 3 != 0;
                uint slot = simd_prefix_exclusive_sum(uint(visible));
                uint count = simd_sum(uint(visible));
                if (visible) payload.meshlets[slot] = lane;
                if (lane >= count) payload.meshlets[lane] = 0;
                if (lane == 0) payload.instance = group.y * 4 + group.x;
                threadgroup_barrier(mem_flags::mem_threadgroup);
                if (lane == 0) {
                    *output = payload;
                    grid.set_threadgroups_per_grid(uint3(count, 1, 1));
                }
            }
        )";
        const char* meshSource = R"(
            #include <metal_stdlib>
            using namespace metal;
            struct Payload { uint instance; array<uint, 32> meshlets; };
            struct Vertex { float4 position [[position]]; };
            [[mesh]] void entry(uint lane [[thread_index_in_threadgroup]],
                               uint3 group [[threadgroup_position_in_grid]],
                               object_data const Payload* payload [[payload]],
                               device atomic_uint* result [[buffer(7)]],
                               mesh<Vertex, void, 64, 124, topology::triangle> output) {
                if (lane == 0) {
                    uint index = payload->instance * 32 + payload->meshlets[group.x];
                    atomic_fetch_add_explicit(result + min(index, 384u), 1u, memory_order_relaxed);
                    output.set_primitive_count(0);
                }
            }
        )";
        wgpu::BindGroupLayoutEntry entry;
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Task | wgpu::ShaderStage::Mesh;
        entry.buffer.type = wgpu::BufferBindingType::Storage;
        wgpu::BindGroupLayoutDescriptor groupDescriptor;
        groupDescriptor.entryCount = 1;
        groupDescriptor.entries = &entry;
        auto groupLayout = device.CreateBindGroupLayout(&groupDescriptor);
        wgpu::PipelineLayoutDescriptor layoutDescriptor;
        layoutDescriptor.bindGroupLayoutCount = 1;
        layoutDescriptor.bindGroupLayouts = &groupLayout;
        auto layout = device.CreatePipelineLayout(&layoutDescriptor);
        wgpu::DawnNativeBinding binding;
        binding.group = 0;
        binding.binding = 0;
        binding.type = wgpu::BufferBindingType::Storage;
        binding.metalIndex = 7;
        wgpu::DawnMeshStage task;
        task.module = NativeModule(wgpu::ShaderStage::Task, taskSource, binding, 32);
        wgpu::DawnMeshPipelineState mesh;
        mesh.task = &task;
        mesh.mesh.module = NativeModule(wgpu::ShaderStage::Mesh, meshSource, binding, 128);
        utils::ComboRenderPipelineDescriptor descriptor;
        descriptor.layout = layout;
        descriptor.nextInChain = &mesh;
        descriptor.cFragment.module = utils::CreateShaderModule(
            device, "@fragment fn main() -> @location(0) vec4f { return vec4f(1.0); }");
        auto pipeline = device.CreateRenderPipeline(&descriptor);
        std::array<uint32_t, 385> zero = {};
        auto result = utils::CreateBufferFromData(device, zero.data(), sizeof(zero),
            wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
        auto group = utils::MakeBindGroup(device, groupLayout, {{0, result}});
        auto renderPass = utils::CreateBasicRenderPass(device, 1, 1);
        auto commands = device.CreateCommandEncoder();
        auto pass = commands.BeginRenderPass(&renderPass.renderPassInfo);
        if (bundle) {
            wgpu::RenderBundleEncoderDescriptor bundleDescriptor;
            bundleDescriptor.colorFormatCount = 1;
            bundleDescriptor.colorFormats = &renderPass.colorFormat;
            auto encoder = device.CreateRenderBundleEncoder(&bundleDescriptor);
            encoder.SetPipeline(pipeline);
            encoder.SetBindGroup(0, group);
            encoder.DrawMeshTasks(4, 3, 1);
            auto recorded = encoder.Finish();
            for (uint32_t iteration = 0; iteration < 100; ++iteration) {
                pass.ExecuteBundles(1, &recorded);
            }
        } else {
            pass.SetPipeline(pipeline);
            pass.SetBindGroup(0, group);
            for (uint32_t iteration = 0; iteration < 100; ++iteration) {
                pass.DrawMeshTasks(4, 3, 1);
            }
        }
        pass.End();
        auto command = commands.Finish();
        queue.Submit(1, &command);
        std::array<uint32_t, 385> expected = {};
        for (uint32_t groupIndex = 0; groupIndex < 12; ++groupIndex) {
            for (uint32_t lane = 0; lane < 32; ++lane) {
                expected[groupIndex * 32 + lane] = (lane + groupIndex % 4) % 3 != 0 ? 100 : 0;
            }
        }
        EXPECT_BUFFER_U32_RANGE_EQ(expected.data(), result, 0, expected.size());
    }

    void CheckDispatch(bool bundle, bool object = true) {
        DAWN_TEST_UNSUPPORTED_IF(!device.HasFeature(wgpu::FeatureName::DawnMeshShader));
        const char* taskSource = R"(
            #include <metal_stdlib>
            using namespace metal;
            struct Payload { uint value; };
            [[object, max_total_threads_per_threadgroup(1), max_total_threadgroups_per_mesh_grid(1)]]
            void entry(constant uint& value [[buffer(5)]], object_data Payload& payload [[payload]],
                       mesh_grid_properties grid) {
                payload.value = value;
                grid.set_threadgroups_per_grid(uint3(1));
            }
        )";
        const std::string meshSource = R"(
            #include <metal_stdlib>
            using namespace metal;
            struct Payload { uint value; };
            struct Vertex { float4 position [[position]]; };
            [[mesh, max_total_threads_per_threadgroup(1)]]
            void entry(object_data const Payload& payload [[payload]],
                       device atomic_uint& result [[buffer(INDEX)]],
                       mesh<Vertex, void, 3, 1, topology::triangle> output) {
                atomic_fetch_add_explicit(&result, payload.value, memory_order_relaxed);
                output.set_vertex(0, Vertex{float4(-1, -1, 0, 1)});
                output.set_vertex(1, Vertex{float4(3, -1, 0, 1)});
                output.set_vertex(2, Vertex{float4(-1, 3, 0, 1)});
                output.set_index(0, 0);
                output.set_index(1, 1);
                output.set_index(2, 2);
                output.set_primitive_count(1);
            }
        )";

        std::array<wgpu::BindGroupLayoutEntry, 2> entries;
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Task;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Mesh;
        entries[1].buffer.type = wgpu::BufferBindingType::Storage;
        wgpu::BindGroupLayoutDescriptor groupDescriptor;
        groupDescriptor.entryCount = entries.size();
        groupDescriptor.entries = entries.data();
        auto groupLayout = device.CreateBindGroupLayout(&groupDescriptor);
        wgpu::PipelineLayoutDescriptor layoutDescriptor;
        layoutDescriptor.bindGroupLayoutCount = 1;
        layoutDescriptor.bindGroupLayouts = &groupLayout;
        auto layout = device.CreatePipelineLayout(&layoutDescriptor);

        wgpu::DawnNativeBinding taskBinding;
        taskBinding.group = 0;
        taskBinding.binding = 0;
        taskBinding.type = wgpu::BufferBindingType::Uniform;
        taskBinding.metalIndex = 5;
        wgpu::DawnMeshStage task;
        task.module = NativeModule(wgpu::ShaderStage::Task, taskSource, taskBinding);
        std::array<wgpu::RenderPipeline, 2> pipelines;
        for (uint32_t index = 0; index < pipelines.size(); ++index) {
            wgpu::DawnNativeBinding meshBinding;
            meshBinding.group = 0;
            meshBinding.binding = 1;
            meshBinding.type = wgpu::BufferBindingType::Storage;
            meshBinding.metalIndex = 7 + index;
            std::string source = meshSource;
            source.replace(source.find("INDEX"), 5, std::to_string(meshBinding.metalIndex));
            wgpu::DawnMeshPipelineState mesh;
            if (object) {
                mesh.task = &task;
            } else {
                const std::string payload = "object_data const Payload& payload [[payload]],";
                source.erase(source.find(payload), payload.size());
                source.replace(source.find("payload.value"), 13, "7u");
            }
            mesh.mesh.module = NativeModule(wgpu::ShaderStage::Mesh, source.c_str(), meshBinding);
            utils::ComboRenderPipelineDescriptor descriptor;
            descriptor.layout = layout;
            descriptor.nextInChain = &mesh;
            descriptor.cFragment.module = utils::CreateShaderModule(
                device, "@fragment fn main() -> @location(0) vec4f { return vec4f(1.0); }");
            pipelines[index] = device.CreateRenderPipeline(&descriptor);
        }

        std::array<uint32_t, 128> values = {};
        values[0] = 3;
        values[64] = 7;
        auto input = utils::CreateBufferFromData(device, values.data(), sizeof(values),
                                                 wgpu::BufferUsage::Uniform);
        auto result = utils::CreateBufferFromData<uint32_t>(
            device, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc, {0});
        auto group = utils::MakeBindGroup(device, groupLayout, {{0, input, 0, 4}, {1, result}});
        auto renderPass = utils::CreateBasicRenderPass(device, 1, 1);
        auto commands = device.CreateCommandEncoder();
        auto pass = commands.BeginRenderPass(&renderPass.renderPassInfo);
        const uint32_t dynamicOffset = 256;
        if (bundle) {
            wgpu::RenderBundleEncoderDescriptor descriptor;
            descriptor.colorFormatCount = 1;
            descriptor.colorFormats = &renderPass.colorFormat;
            auto encoder = device.CreateRenderBundleEncoder(&descriptor);
            encoder.SetBindGroup(0, group, 1, &dynamicOffset);
            for (const auto& pipeline : pipelines) {
                encoder.SetPipeline(pipeline);
                encoder.DrawMeshTasks(1, 1, 1);
            }
            auto recorded = encoder.Finish();
            pass.ExecuteBundles(1, &recorded);
        } else {
            pass.SetBindGroup(0, group, 1, &dynamicOffset);
            for (const auto& pipeline : pipelines) {
                pass.SetPipeline(pipeline);
                pass.DrawMeshTasks(1, 1, 1);
            }
        }
        pass.End();
        auto command = commands.Finish();
        queue.Submit(1, &command);
        EXPECT_BUFFER_U32_EQ(14u, result, 0);
        EXPECT_PIXEL_RGBA8_EQ(utils::RGBA8::kWhite, renderPass.color, 0, 0);
    }
};

TEST_P(MetalMeshShaderTests, DirectPayloadDynamicOffsetsAndPipelineSwitch) {
    CheckDispatch(false);
}

TEST_P(MetalMeshShaderTests, BundlePayloadDynamicOffsetsAndPipelineSwitch) {
    CheckDispatch(true);
}

TEST_P(MetalMeshShaderTests, DirectMeshOnlyPipelineSwitch) {
    CheckDispatch(false, false);
}

TEST_P(MetalMeshShaderTests, BundleMeshOnlyPipelineSwitch) {
    CheckDispatch(true, false);
}

TEST_P(MetalMeshShaderTests, CompactedPayloadAcrossObjectGroups) {
    CheckCompactedPayload(false);
}

TEST_P(MetalMeshShaderTests, CompactedPayloadAcrossBundleReplay) {
    CheckCompactedPayload(true);
}

DAWN_INSTANTIATE_TEST(MetalMeshShaderTests, MetalBackend());

}  // namespace
}  // namespace dawn
