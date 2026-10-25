// Copyright 2026 The Dawn & Tint Authors
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

#include <type_traits>

// The native header comes first here, and the domain header is included beside it.
#include "webgpu/webgpu_cpp.h"
// Comment to prevent reordering.
#include "dawn/wire/client/webgpu_cpp_domain.h"

namespace dawn::wire {
namespace {

namespace client = wgpu::dawn::wire::client;

// Objects and structures are the domain's own, with the layouts of the native ones.
static_assert(!std::is_same_v<client::Device, wgpu::Device>);
static_assert(!std::is_convertible_v<wgpu::Device, client::Device>);
static_assert(!std::is_convertible_v<client::Device, wgpu::Device>);
static_assert(!std::is_same_v<client::RenderPipelineDescriptor, wgpu::RenderPipelineDescriptor>);
static_assert(sizeof(client::Device) == sizeof(wgpu::Device));
static_assert(sizeof(client::RenderPipelineDescriptor) == sizeof(wgpu::RenderPipelineDescriptor));
static_assert(alignof(client::RenderPipelineDescriptor) == alignof(wgpu::RenderPipelineDescriptor));

// Enums, bitmasks, Bool and StringView are the native header's.
static_assert(std::is_same_v<decltype(client::BufferDescriptor::usage), wgpu::BufferUsage>);
static_assert(std::is_same_v<decltype(client::BufferDescriptor::label), wgpu::StringView>);
static_assert(std::is_same_v<decltype(client::BufferDescriptor::mappedAtCreation), wgpu::Bool>);
static_assert(std::is_same_v<decltype(client::TextureDescriptor::format), wgpu::TextureFormat>);

// Each domain names its own objects and structures.
static_assert(std::is_same_v<wgpu::ApiTypes::Device, wgpu::Device>);
static_assert(std::is_same_v<client::ApiTypes::Device, client::Device>);
static_assert(
    std::is_same_v<client::ApiTypes::RenderPipelineDescriptor, client::RenderPipelineDescriptor>);
static_assert(std::is_same_v<client::ApiTypes::StringView, wgpu::StringView>);

}  // anonymous namespace
}  // namespace dawn::wire
