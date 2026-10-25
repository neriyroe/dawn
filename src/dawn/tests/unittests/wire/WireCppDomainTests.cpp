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

// The domain header comes first: it brings in the native header whose types it shares.
#include "dawn/wire/client/webgpu_cpp_domain.h"
#include "src/dawn/tests/unittests/wire/WireTest.h"

namespace dawn::wire {
namespace {

using testing::Return;

namespace client = wgpu::dawn::wire::client;

class WireCppDomainTests : public WireTest {};

// Test that the domain's objects call the wire client: an encoder it creates reaches the server.
TEST_F(WireCppDomainTests, ObjectsCallTheWireClient) {
    client::Device wireDevice = client::Device(device.Get());
    client::CommandEncoder encoder = wireDevice.CreateCommandEncoder();

    WGPUCommandEncoder apiEncoder = api.GetNewCommandEncoder();
    EXPECT_CALL(api, DeviceCreateCommandEncoder(apiDevice, nullptr)).WillOnce(Return(apiEncoder));
    FlushClient();
}

}  // anonymous namespace
}  // namespace dawn::wire
