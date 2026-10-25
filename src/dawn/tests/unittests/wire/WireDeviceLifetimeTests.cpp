// Copyright 2023 The Dawn & Tint Authors
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

#include <gtest/gtest.h>
#include <webgpu/webgpu.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "dawn/dawn_proc.h"
#include "dawn/native/DawnNative.h"
#include "dawn/wire/WireClient.h"
#include "dawn/wire/WireServer.h"
#include "src/dawn/native/Instance.h"
#include "src/dawn/utils/TerribleCommandBuffer.h"
#include "src/dawn/utils/WireHelper.h"

namespace dawn {
namespace {

static WGPUDevice lastBackendDevice = nullptr;

class WireDeviceLifetimeTests : public testing::Test {
  public:
    WireDeviceLifetimeTests()
        : nativeProcs(BuildProcs()),
          wireHelper(utils::CreateWireHelper(nativeProcs, /* useWire */ true)) {
        static constexpr auto kMultipleDevicesPerAdapter =
            wgpu::InstanceFeatureName::MultipleDevicesPerAdapter;
        wgpu::InstanceDescriptor instanceDesc = {.requiredFeatureCount = 1,
                                                 .requiredFeatures = &kMultipleDevicesPerAdapter};
        instance = wireHelper->CreateInstances(&instanceDesc).first;
    }

  protected:
    void SetUp() override {
        wgpu::RequestAdapterOptions options = {};
        options.backendType = wgpu::BackendType::Null;

        instance.RequestAdapter(
            &options, wgpu::CallbackMode::AllowSpontaneous,
            [this](wgpu::RequestAdapterStatus status, wgpu::Adapter a, wgpu::StringView) {
                ASSERT_EQ(status, wgpu::RequestAdapterStatus::Success);
                adapter = std::move(a);
            });
        ASSERT_TRUE(wireHelper->FlushClient());
        ASSERT_TRUE(wireHelper->FlushServer());
        ASSERT_NE(adapter, nullptr);
    }

    const DawnProcTable nativeProcs;
    std::unique_ptr<utils::WireHelper> wireHelper;

    wgpu::Instance instance;
    wgpu::Adapter adapter;

  private:
    // Set up the proc table which will save the latest WGPUDevice created from requestDevice in
    // static variable `lastBackendDevice`. This lets tests control the wire device and the native
    // backend device separately.
    DawnProcTable BuildProcs() {
        DawnProcTable procs = native::GetProcs();
        procs.adapterRequestDevice = [](WGPUAdapter self, const WGPUDeviceDescriptor* desc,
                                        WGPURequestDeviceCallbackInfo callbackInfo) {
            return native::GetProcs().adapterRequestDevice(
                self, desc,
                {nullptr, callbackInfo.mode,
                 [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                    void* userdata, void*) {
                     lastBackendDevice = device;
                     std::unique_ptr<WGPURequestDeviceCallbackInfo> info(
                         static_cast<WGPURequestDeviceCallbackInfo*>(userdata));
                     info->callback(status, device, message, info->userdata1, info->userdata2);
                 },
                 new WGPURequestDeviceCallbackInfo(callbackInfo), nullptr});
        };

        return procs;
    }
};

// Drop the device from the wire, then call its uncaptured error callback.
// This is a regression test for crbug.com/1430221 where these operations caused a use-after-free.
TEST_F(WireDeviceLifetimeTests, DeviceDroppedFromWireThenUncapturedErrorCallback) {
    wgpu::Device device;
    wgpu::DeviceDescriptor deviceDesc = {};

    adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                          [&device](wgpu::RequestDeviceStatus status, wgpu::Device d,
                                    wgpu::StringView) { device = std::move(d); });
    ASSERT_TRUE(wireHelper->FlushClient());
    ASSERT_TRUE(wireHelper->FlushServer());
    ASSERT_NE(device, nullptr);

    // Destroy the device.
    device.Destroy();

    // Inject an error.
    device.InjectError(wgpu::ErrorType::Validation, "injected error");

    // Drop the device, but keep the server-side device alive.
    // This prevents the callbacks from being flushed yet.
    WGPUDevice oldDevice = lastBackendDevice;
    nativeProcs.deviceAddRef(oldDevice);
    device = nullptr;

    // Request a new device. This overrides the wire's device-related data.
    adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                          [&device](wgpu::RequestDeviceStatus status, wgpu::Device d,
                                    wgpu::StringView) { device = std::move(d); });
    ASSERT_TRUE(wireHelper->FlushClient());
    ASSERT_TRUE(wireHelper->FlushServer());
    ASSERT_NE(device, nullptr);

    // Finally, release the old retained device which will flush any of its outstanding
    // callbacks.
    nativeProcs.deviceRelease(oldDevice);
}

// Drop the device from the wire, then call its logging callback.
// This is a regression test for crbug.com/1430221 where these operations caused a use-after-free.
TEST_F(WireDeviceLifetimeTests, DeviceDroppedFromWireThenLoggingCallback) {
    wgpu::Device device;
    wgpu::DeviceDescriptor deviceDesc = {};
    adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                          [&device](wgpu::RequestDeviceStatus status, wgpu::Device d,
                                    wgpu::StringView) { device = std::move(d); });
    ASSERT_TRUE(wireHelper->FlushClient());
    ASSERT_TRUE(wireHelper->FlushServer());
    ASSERT_NE(device, nullptr);

    wgpu::ShaderModuleDescriptor shaderModuleDesc = {};
    wgpu::ShaderSourceWGSL wgslDesc = {};
    shaderModuleDesc.nextInChain = &wgslDesc;
    wgslDesc.code = "@compute @workgroup_size(64) fn main() {}";

    // Create a shader module so the transformed shaders are dumped.
    device.CreateShaderModule(&shaderModuleDesc);

    // Destroy the device.
    device.Destroy();

    // Drop the device, but keep the server-side device alive.
    // This prevents the callbacks from being flushed yet.
    WGPUDevice oldDevice = lastBackendDevice;
    nativeProcs.deviceAddRef(oldDevice);
    device = nullptr;

    // Request a new device. This overrides the wire's device-related data.
    adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                          [&device](wgpu::RequestDeviceStatus status, wgpu::Device d,
                                    wgpu::StringView) { device = std::move(d); });
    ASSERT_TRUE(wireHelper->FlushClient());
    ASSERT_TRUE(wireHelper->FlushServer());
    ASSERT_NE(device, nullptr);

    // Finally, release the old retained device which will flush any of its outstanding
    // callbacks.
    nativeProcs.deviceRelease(oldDevice);
}

// Drop the device from the wire, then call its lost callback.
// This is a regression test for crbug.com/1430221 where these operations caused a use-after-free.
TEST_F(WireDeviceLifetimeTests, DeviceDroppedFromWireThenLostCallback) {
    wgpu::Device device;
    wgpu::DeviceDescriptor deviceDesc = {};
    adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                          [&device](wgpu::RequestDeviceStatus status, wgpu::Device d,
                                    wgpu::StringView) { device = std::move(d); });
    ASSERT_TRUE(wireHelper->FlushClient());
    ASSERT_TRUE(wireHelper->FlushServer());
    ASSERT_NE(device, nullptr);

    // Drop the device, but keep the server-side device alive.
    // This prevents the callbacks from being flushed yet.
    WGPUDevice oldDevice = lastBackendDevice;
    nativeProcs.deviceAddRef(oldDevice);
    device = nullptr;

    // Destroy the device to enqueue calling the lost callback.
    nativeProcs.deviceDestroy(oldDevice);

    // Request a new device. This overrides the wire's device-related data.
    adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                          [&device](wgpu::RequestDeviceStatus status, wgpu::Device d,
                                    wgpu::StringView) { device = std::move(d); });
    ASSERT_TRUE(wireHelper->FlushClient());
    ASSERT_TRUE(wireHelper->FlushServer());
    ASSERT_NE(device, nullptr);

    // Finally, release the old retained device which will flush any of its outstanding
    // callbacks.
    nativeProcs.deviceRelease(oldDevice);
}

// A wire over an embedder's own null backend device, which it injects instead of requesting.
class WireImportedDeviceLifetimeTests : public testing::Test {
  protected:
    void SetUp() override {
        wgpu::RequestAdapterOptions options = {};
        options.backendType = wgpu::BackendType::Null;
        std::vector<native::Adapter> adapters = mNativeInstance.EnumerateAdapters(&options);
        ASSERT_FALSE(adapters.empty());
        mNativeAdapter = adapters[0].Get();
        mNativeProcs.adapterAddRef(mNativeAdapter);

        WGPUDeviceDescriptor nativeDesc = {};
        nativeDesc.deviceLostCallbackInfo = {
            nullptr, WGPUCallbackMode_AllowSpontaneous,
            [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView, void* self, void*) {
                static_cast<WireImportedDeviceLifetimeTests*>(self)->mNativeLoss = reason;
            },
            this, nullptr};
        mNativeDevice = adapters[0].CreateDevice(&nativeDesc);
        ASSERT_NE(mNativeDevice, nullptr);

        mC2s = std::make_unique<utils::TerribleCommandBuffer>();
        mS2c = std::make_unique<utils::TerribleCommandBuffer>();
        wire::WireServerDescriptor serverDesc = {};
        serverDesc.procs = &mNativeProcs;
        serverDesc.serializer = mS2c.get();
        mServer = std::make_unique<wire::WireServer>(serverDesc);
        mC2s->SetHandler(mServer.get());
        wire::WireClientDescriptor clientDesc = {};
        clientDesc.serializer = mC2s.get();
        mClient = std::make_unique<wire::WireClient>(clientDesc);
        mS2c->SetHandler(mClient.get());
        dawnProcSetProcs(&wire::client::GetProcs());

        // The import: every reservation first, then the injections that answer them.
        wire::ReservedInstance instance = mClient->ReserveInstance();
        constexpr WGPURequestAdapterCallbackInfo kAdapterAnswer = {
            nullptr, WGPUCallbackMode_AllowSpontaneous, nullptr, nullptr, nullptr};
        wire::ReservedAdapter adapter = mClient->ReserveAdapter(instance.instance, kAdapterAnswer);
        WGPUDeviceDescriptor deviceDesc = {};
        deviceDesc.deviceLostCallbackInfo = {
            nullptr, WGPUCallbackMode_AllowSpontaneous,
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {}, nullptr,
            nullptr};
        constexpr WGPURequestDeviceCallbackInfo kDeviceAnswer = {
            nullptr, WGPUCallbackMode_AllowSpontaneous, nullptr, nullptr, nullptr};
        wire::ReservedDevice device =
            mClient->ReserveDevice(adapter.adapter, &deviceDesc, kDeviceAnswer);
        ASSERT_TRUE(mServer->InjectInstance(mNativeInstance.Get(), instance.handle));
        ASSERT_TRUE(mServer->InjectAdapter(mNativeAdapter, adapter.handle, adapter.instanceHandle,
                                           adapter.future));
        ASSERT_TRUE(mServer->InjectDevice(mNativeDevice, device.handle, device.instanceHandle,
                                          device.future));
        ASSERT_TRUE(mS2c->Flush());
        mInstance = wgpu::Instance::Acquire(instance.instance);
        mAdapter = wgpu::Adapter::Acquire(adapter.adapter);
        mDevice = wgpu::Device::Acquire(device.device);
    }

    void TearDown() override {
        mDevice = nullptr;
        mAdapter = nullptr;
        mInstance = nullptr;
        if (mS2c != nullptr) {
            mS2c->SetHandler(nullptr);
        }
        mClient = nullptr;
        if (mC2s != nullptr) {
            mC2s->SetHandler(nullptr);
        }
        mServer = nullptr;
        dawnProcSetProcs(nullptr);
        if (mNativeDevice != nullptr) {
            mNativeProcs.deviceRelease(mNativeDevice);
        }
        if (mNativeAdapter != nullptr) {
            mNativeProcs.adapterRelease(mNativeAdapter);
        }
    }

    // Whether the embedder's device is still standing once native events have run.
    bool NativeDeviceStands() {
        mNativeProcs.instanceProcessEvents(mNativeInstance.Get());
        return !mNativeLoss.has_value();
    }

    const DawnProcTable mNativeProcs = native::GetProcs();
    native::Instance mNativeInstance;
    WGPUAdapter mNativeAdapter = nullptr;
    WGPUDevice mNativeDevice = nullptr;
    std::optional<WGPUDeviceLostReason> mNativeLoss;

    std::unique_ptr<utils::TerribleCommandBuffer> mC2s;
    std::unique_ptr<utils::TerribleCommandBuffer> mS2c;
    std::unique_ptr<wire::WireServer> mServer;
    std::unique_ptr<wire::WireClient> mClient;

    wgpu::Instance mInstance;
    wgpu::Adapter mAdapter;
    wgpu::Device mDevice;
};

// Test that an imported device outlives the client: its last reference returns the server's
// reference, and neither it nor the client's teardown destroys the device.
TEST_F(WireImportedDeviceLifetimeTests, ImportedDeviceOutlivesTheClient) {
    wgpu::BufferDescriptor descriptor = {};
    descriptor.size = 4;
    descriptor.usage = wgpu::BufferUsage::CopyDst;
    wgpu::Buffer buffer = mDevice.CreateBuffer(&descriptor);
    ASSERT_TRUE(mC2s->Flush());

    buffer = nullptr;
    mDevice = nullptr;
    ASSERT_TRUE(mC2s->Flush());
    EXPECT_FALSE(mServer->IsDeviceKnown(mNativeDevice));
    EXPECT_TRUE(NativeDeviceStands());

    mAdapter = nullptr;
    mInstance = nullptr;
    mS2c->SetHandler(nullptr);
    mClient = nullptr;
    ASSERT_TRUE(mC2s->Flush());
    EXPECT_TRUE(NativeDeviceStands());
}

// Test that an imported device the client still holds outlives the server, and that the embedder
// still owns it and destroys it itself.
TEST_F(WireImportedDeviceLifetimeTests, ImportedDeviceOutlivesTheServer) {
    ASSERT_TRUE(mServer->IsDeviceKnown(mNativeDevice));
    mC2s->SetHandler(nullptr);
    mServer = nullptr;
    EXPECT_TRUE(NativeDeviceStands());

    mNativeProcs.deviceDestroy(mNativeDevice);
    mNativeProcs.instanceProcessEvents(mNativeInstance.Get());
    EXPECT_EQ(mNativeLoss, WGPUDeviceLostReason_Destroyed);
}

// Test that the client's explicit Destroy() still destroys an imported device.
TEST_F(WireImportedDeviceLifetimeTests, DestroyDestroysTheImportedDevice) {
    mDevice.Destroy();
    ASSERT_TRUE(mC2s->Flush());
    mNativeProcs.instanceProcessEvents(mNativeInstance.Get());
    EXPECT_EQ(mNativeLoss, WGPUDeviceLostReason_Destroyed);
}

}  // anonymous namespace
}  // namespace dawn
