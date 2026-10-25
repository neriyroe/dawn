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

#include <array>

#include "dawn/wire/WireClient.h"
#include "dawn/wire/WireServer.h"
#include "src/dawn/common/StringViewUtils.h"
#include "src/dawn/tests/MockCallback.h"
#include "src/dawn/tests/StringViewMatchers.h"
#include "src/dawn/tests/unittests/wire/WireFutureTest.h"
#include "src/dawn/tests/unittests/wire/WireTest.h"
#include "webgpu/webgpu_cpp.h"

namespace dawn::wire {
namespace {

using testing::_;
using testing::AnyNumber;
using testing::EmptySizedString;
using testing::InSequence;
using testing::IsNull;
using testing::Mock;
using testing::MockCallback;
using testing::NotNull;
using testing::Pointee;
using testing::Return;
using testing::SizedString;
using testing::WithArg;

constexpr std::array<WGPUFeatureName, 2> kAdapterFeatures = {WGPUFeatureName_DawnMeshShader,
                                                             WGPUFeatureName_TimestampQuery};
constexpr std::array<WGPUFeatureName, 1> kDeviceFeatures = {WGPUFeatureName_DawnMeshShader};
// HostMappedPointer is a native feature that the wire cannot carry.
constexpr std::array<WGPUFeatureName, 2> kUncarriedFeatures = {WGPUFeatureName_DawnMeshShader,
                                                               WGPUFeatureName_HostMappedPointer};
constexpr uint32_t kVendorID = 0x10de;
constexpr uint32_t kMaxTextureDimension2D = 12345;
constexpr uint32_t kMaxBindGroups = 7;

// An adapter and a device the server already has, reserved on the client and injected on the
// server, as an embedder that creates its device natively imports it.
class WireInjectDeviceTests : public WireFutureTestWithParamsBase<> {
  protected:
    void SetUp() override {
        WireFutureTestWithParamsBase<>::SetUp();
        EXPECT_CALL(mDeviceLostCb, Call).Times(AnyNumber());
    }

    WGPUCallbackMode Mode() const { return static_cast<WGPUCallbackMode>(GetParam().callbackMode); }

    ReservedAdapter ReserveAdapter() {
        ReservedAdapter reserved = GetWireClient()->ReserveAdapter(
            instance.Get(),
            {nullptr, Mode(), mAdapterCb.Callback(), nullptr, mAdapterCb.MakeUserdata(this)});
        mFutureIDs.push_back(reserved.future.id);
        return reserved;
    }

    ReservedDevice ReserveDevice(WGPUAdapter onAdapter) {
        WGPUDeviceDescriptor descriptor = {};
        descriptor.deviceLostCallbackInfo = {nullptr, WGPUCallbackMode_AllowSpontaneous,
                                             mDeviceLostCb.Callback(), nullptr,
                                             mDeviceLostCb.MakeUserdata(this)};
        ReservedDevice reserved = GetWireClient()->ReserveDevice(
            onAdapter, &descriptor,
            {nullptr, Mode(), mDeviceCb.Callback(), nullptr, mDeviceCb.MakeUserdata(this)});
        mFutureIDs.push_back(reserved.future.id);
        return reserved;
    }

    // The queries the server answers an injected adapter's reply with.
    void ExpectAdapterQueries(WGPUAdapter serverAdapter) {
        EXPECT_CALL(api, AdapterHasFeature(serverAdapter, _)).WillRepeatedly(Return(false));
        EXPECT_CALL(api, AdapterGetInfo(serverAdapter, NotNull()))
            .WillOnce(WithArg<1>([](WGPUAdapterInfo* info) {
                info->vendor = ToOutputStringView("injected-vendor");
                info->architecture = kEmptyOutputStringView;
                info->device = kEmptyOutputStringView;
                info->description = kEmptyOutputStringView;
                info->backendType = WGPUBackendType_D3D12;
                info->vendorID = kVendorID;
                return WGPUStatus_Success;
            }));
        EXPECT_CALL(api, AdapterGetLimits(serverAdapter, NotNull()))
            .WillOnce(WithArg<1>([](WGPULimits* limits) {
                limits->maxTextureDimension2D = kMaxTextureDimension2D;
                return WGPUStatus_Success;
            }));
        EXPECT_CALL(api, AdapterGetFeatures(serverAdapter, NotNull()))
            .WillOnce(WithArg<1>([](WGPUSupportedFeatures* features) {
                *features = {kAdapterFeatures.size(), kAdapterFeatures.data()};
            }));
    }

    // The queries the server answers an injected device's reply with.
    template <size_t N>
    void ExpectDeviceQueries(WGPUDevice serverDevice,
                             const std::array<WGPUFeatureName, N>& deviceFeatures) {
        EXPECT_CALL(api, DeviceGetFeatures(serverDevice, NotNull()))
            .WillOnce(
                WithArg<1>([count = deviceFeatures.size(), data = deviceFeatures.data()](
                               WGPUSupportedFeatures* features) { *features = {count, data}; }));
        EXPECT_CALL(api, DeviceGetLimits(serverDevice, NotNull()))
            .WillRepeatedly(WithArg<1>([](WGPULimits* limits) {
                limits->maxBindGroups = kMaxBindGroups;
                return WGPUStatus_Success;
            }));
    }

    bool InjectAdapter(WGPUAdapter serverAdapter, const ReservedAdapter& reserved) {
        return GetWireServer()->InjectAdapter(serverAdapter, reserved.handle,
                                              reserved.instanceHandle, reserved.future);
    }

    bool InjectDevice(WGPUDevice serverDevice, const ReservedDevice& reserved) {
        return GetWireServer()->InjectDevice(serverDevice, reserved.handle, reserved.instanceHandle,
                                             reserved.future);
    }

    // Imports a device on the client's adapter and drops the reference its callback hands over,
    // leaving the reservation's.
    wgpu::Device ImportDevice(WGPUDevice serverDevice) {
        ReservedDevice reserved = ReserveDevice(adapter.Get());
        EXPECT_CALL(api, DeviceAddRef(serverDevice));
        ExpectDeviceQueries(serverDevice, kDeviceFeatures);
        EXPECT_TRUE(InjectDevice(serverDevice, reserved));
        FlushFutures();
        EXPECT_CALL(mDeviceCb, Call(WGPURequestDeviceStatus_Success, reserved.device, _, _, this))
            .WillOnce(WithArg<1>([](WGPUDevice answered) { wgpuDeviceRelease(answered); }));
        FlushCallbacks();
        EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mDeviceCb));
        return wgpu::Device::Acquire(reserved.device);
    }

    MockCallback<WGPURequestAdapterCallback> mAdapterCb;
    MockCallback<WGPURequestDeviceCallback> mDeviceCb;
    MockCallback<WGPUDeviceLostCallback> mDeviceLostCb;
};

DAWN_INSTANTIATE_WIRE_FUTURE_TEST_P(WireInjectDeviceTests);

// Test that an injected adapter answers its reservation with the server adapter's information.
TEST_P(WireInjectDeviceTests, InjectAdapterAnswersTheReservation) {
    ReservedAdapter reserved = ReserveAdapter();

    WGPUAdapter serverAdapter = api.GetNewAdapter();
    EXPECT_CALL(api, AdapterAddRef(serverAdapter));
    ExpectAdapterQueries(serverAdapter);
    ASSERT_TRUE(InjectAdapter(serverAdapter, reserved));
    FlushFutures();

    EXPECT_CALL(mAdapterCb, Call(WGPURequestAdapterStatus_Success, reserved.adapter,
                                 EmptySizedString(), nullptr, this))
        .WillOnce(WithArg<1>([](WGPUAdapter answered) { wgpuAdapterRelease(answered); }));
    FlushCallbacks();
    Mock::VerifyAndClearExpectations(&mAdapterCb);

    wgpu::Adapter injected = wgpu::Adapter::Acquire(reserved.adapter);
    wgpu::AdapterInfo info;
    ASSERT_EQ(injected.GetInfo(&info), wgpu::Status::Success);
    EXPECT_EQ(info.vendorID, kVendorID);
    EXPECT_EQ(info.backendType, wgpu::BackendType::D3D12);
    EXPECT_THAT(info.vendor, SizedString("injected-vendor"));

    wgpu::Limits limits;
    ASSERT_EQ(injected.GetLimits(&limits), wgpu::Status::Success);
    EXPECT_EQ(limits.maxTextureDimension2D, kMaxTextureDimension2D);
    EXPECT_TRUE(injected.HasFeature(wgpu::FeatureName::DawnMeshShader));
    EXPECT_TRUE(injected.HasFeature(wgpu::FeatureName::TimestampQuery));

    // The server took one reference, which the client's release returns.
    injected = nullptr;
    EXPECT_CALL(api, AdapterRelease(serverAdapter));
    FlushClient();
}

// Test that an injected device answers its reservation with the server device's limits and
// features, and that the client's commands then reach it.
TEST_P(WireInjectDeviceTests, InjectDeviceAnswersTheReservation) {
    ReservedDevice reserved = ReserveDevice(adapter.Get());

    WGPUDevice serverDevice = api.GetNewDevice();
    EXPECT_FALSE(GetWireServer()->IsDeviceKnown(serverDevice));
    EXPECT_CALL(api, DeviceAddRef(serverDevice));
    ExpectDeviceQueries(serverDevice, kDeviceFeatures);
    // The device's callbacks stay the embedder's.
    EXPECT_CALL(api, OnDeviceSetLoggingCallback(serverDevice, _)).Times(0);
    ASSERT_TRUE(InjectDevice(serverDevice, reserved));
    EXPECT_TRUE(GetWireServer()->IsDeviceKnown(serverDevice));
    EXPECT_EQ(GetWireServer()->GetDevice(reserved.handle.id, reserved.handle.generation),
              serverDevice);
    FlushFutures();

    EXPECT_CALL(mDeviceCb, Call(WGPURequestDeviceStatus_Success, reserved.device,
                                EmptySizedString(), nullptr, this))
        .WillOnce(WithArg<1>([](WGPUDevice answered) { wgpuDeviceRelease(answered); }));
    FlushCallbacks();
    Mock::VerifyAndClearExpectations(&mDeviceCb);

    wgpu::Device injected = wgpu::Device::Acquire(reserved.device);
    wgpu::Limits limits;
    ASSERT_EQ(injected.GetLimits(&limits), wgpu::Status::Success);
    EXPECT_EQ(limits.maxBindGroups, kMaxBindGroups);
    EXPECT_TRUE(injected.HasFeature(wgpu::FeatureName::DawnMeshShader));
    EXPECT_EQ(injected.GetAdapter().Get(), adapter.Get());

    wgpu::CommandEncoder encoder = injected.CreateCommandEncoder();
    wgpu::Queue injectedQueue = injected.GetQueue();
    WGPUCommandEncoder serverEncoder = api.GetNewCommandEncoder();
    WGPUQueue serverQueue = api.GetNewQueue();
    EXPECT_CALL(api, DeviceCreateCommandEncoder(serverDevice, _)).WillOnce(Return(serverEncoder));
    EXPECT_CALL(api, DeviceGetQueue(serverDevice)).WillOnce(Return(serverQueue));
    FlushClient();
}

// Test that an adapter and a device reserved together, before either is injected, are answered in
// the order they were reserved: the import an embedder makes in one step.
TEST_P(WireInjectDeviceTests, InjectAdapterAndDeviceReservedTogether) {
    ReservedAdapter reservedAdapter = ReserveAdapter();
    ReservedDevice reservedDevice = ReserveDevice(reservedAdapter.adapter);

    WGPUAdapter serverAdapter = api.GetNewAdapter();
    WGPUDevice serverDevice = api.GetNewDevice();
    EXPECT_CALL(api, AdapterAddRef(serverAdapter));
    EXPECT_CALL(api, DeviceAddRef(serverDevice));
    ExpectAdapterQueries(serverAdapter);
    ExpectDeviceQueries(serverDevice, kDeviceFeatures);
    ASSERT_TRUE(InjectAdapter(serverAdapter, reservedAdapter));
    ASSERT_TRUE(InjectDevice(serverDevice, reservedDevice));
    FlushFutures();

    {
        InSequence answers;
        EXPECT_CALL(mAdapterCb, Call(WGPURequestAdapterStatus_Success, reservedAdapter.adapter,
                                     EmptySizedString(), nullptr, this))
            .WillOnce(WithArg<1>([](WGPUAdapter answered) { wgpuAdapterRelease(answered); }));
        EXPECT_CALL(mDeviceCb, Call(WGPURequestDeviceStatus_Success, reservedDevice.device,
                                    EmptySizedString(), nullptr, this))
            .WillOnce(WithArg<1>([](WGPUDevice answered) { wgpuDeviceRelease(answered); }));
    }
    FlushCallbacks();
    Mock::VerifyAndClearExpectations(&mAdapterCb);
    Mock::VerifyAndClearExpectations(&mDeviceCb);

    wgpu::Adapter injectedAdapter = wgpu::Adapter::Acquire(reservedAdapter.adapter);
    wgpu::Device injectedDevice = wgpu::Device::Acquire(reservedDevice.device);
    EXPECT_EQ(injectedDevice.GetAdapter().Get(), injectedAdapter.Get());
    wgpu::AdapterInfo info;
    ASSERT_EQ(injectedDevice.GetAdapterInfo(&info), wgpu::Status::Success);
    EXPECT_EQ(info.vendorID, kVendorID);
}

// Test that a device with a feature the wire cannot carry is refused, as a requested one is, and
// that the reference the server took is returned.
TEST_P(WireInjectDeviceTests, InjectDeviceRefusesAFeatureTheWireCannotCarry) {
    ReservedDevice reserved = ReserveDevice(adapter.Get());

    WGPUDevice serverDevice = api.GetNewDevice();
    EXPECT_CALL(api, DeviceAddRef(serverDevice));
    EXPECT_CALL(api, DeviceRelease(serverDevice));
    ExpectDeviceQueries(serverDevice, kUncarriedFeatures);
    EXPECT_FALSE(InjectDevice(serverDevice, reserved));
    EXPECT_FALSE(GetWireServer()->IsDeviceKnown(serverDevice));
    FlushFutures();

    EXPECT_CALL(mDeviceCb, Call(WGPURequestDeviceStatus_Error, IsNull(),
                                SizedString("Requested feature not supported."), nullptr, this));
    EXPECT_CALL(mDeviceLostCb,
                Call(Pointee(IsNull()), WGPUDeviceLostReason_FailedCreation, _, nullptr, this));
    FlushCallbacks();
    Mock::VerifyAndClearExpectations(&mDeviceCb);
    Mock::VerifyAndClearExpectations(&mDeviceLostCb);

    wgpuDeviceRelease(reserved.device);
    FlushClient();
}

// Test that an injection needs the instance its reservation was made on.
TEST_P(WireInjectDeviceTests, InjectNeedsTheReservingInstance) {
    ReservedAdapter reservedAdapter = ReserveAdapter();
    ReservedDevice reservedDevice = ReserveDevice(adapter.Get());

    WGPUAdapter serverAdapter = api.GetNewAdapter();
    WGPUDevice serverDevice = api.GetNewDevice();
    EXPECT_CALL(api, AdapterAddRef(_)).Times(0);
    EXPECT_CALL(api, DeviceAddRef(_)).Times(0);

    Handle staleInstance = reservedAdapter.instanceHandle;
    staleInstance.generation++;
    EXPECT_FALSE(GetWireServer()->InjectAdapter(serverAdapter, reservedAdapter.handle,
                                                staleInstance, reservedAdapter.future));
    Handle unknownInstance = reservedDevice.instanceHandle;
    unknownInstance.id += 100;
    EXPECT_FALSE(GetWireServer()->InjectDevice(serverDevice, reservedDevice.handle, unknownInstance,
                                               reservedDevice.future));
    Mock::VerifyAndClearExpectations(&api);

    // The reservations stay pending until the wire disconnects.
    EXPECT_CALL(mAdapterCb, Call(WGPURequestAdapterStatus_CallbackCancelled, IsNull(), _, _, _));
    EXPECT_CALL(mDeviceCb, Call(WGPURequestDeviceStatus_CallbackCancelled, IsNull(), _, _, _));
    GetWireClient()->Disconnect();
    wgpuAdapterRelease(reservedAdapter.adapter);
    wgpuDeviceRelease(reservedDevice.device);
}

// Test that a reservation is injected once: its handle is then in use.
TEST_P(WireInjectDeviceTests, InjectReservationOnce) {
    ReservedAdapter reserved = ReserveAdapter();

    WGPUAdapter serverAdapter = api.GetNewAdapter();
    EXPECT_CALL(api, AdapterAddRef(serverAdapter));
    ExpectAdapterQueries(serverAdapter);
    ASSERT_TRUE(InjectAdapter(serverAdapter, reserved));
    EXPECT_FALSE(InjectAdapter(serverAdapter, reserved));
    FlushFutures();

    EXPECT_CALL(mAdapterCb, Call(WGPURequestAdapterStatus_Success, reserved.adapter, _, _, this))
        .WillOnce(WithArg<1>([](WGPUAdapter answered) { wgpuAdapterRelease(answered); }));
    FlushCallbacks();
    wgpuAdapterRelease(reserved.adapter);
}

// Test that a disconnect before the injection cancels the reservations' requests.
TEST_P(WireInjectDeviceTests, DisconnectCancelsReservations) {
    ReservedAdapter reservedAdapter = ReserveAdapter();
    ReservedDevice reservedDevice = ReserveDevice(reservedAdapter.adapter);

    EXPECT_CALL(mAdapterCb, Call(WGPURequestAdapterStatus_CallbackCancelled, IsNull(), _, _, _));
    EXPECT_CALL(mDeviceCb, Call(WGPURequestDeviceStatus_CallbackCancelled, IsNull(), _, _, _));
    GetWireClient()->Disconnect();

    wgpuDeviceRelease(reservedDevice.device);
    wgpuAdapterRelease(reservedAdapter.adapter);
}

// Test that dropping the client's last reference to an imported device cancels its lost callback,
// and that the server then returns its reference without destroying the device.
TEST_P(WireInjectDeviceTests, DroppingAnImportedDeviceReleasesIt) {
    WGPUDevice serverDevice = api.GetNewDevice();
    wgpu::Device injected = ImportDevice(serverDevice);
    FlushClient();

    EXPECT_CALL(mDeviceLostCb, Call(_, WGPUDeviceLostReason_CallbackCancelled,
                                    SizedString("Device was released."), nullptr, this));
    injected = nullptr;
    Mock::VerifyAndClearExpectations(&mDeviceLostCb);

    EXPECT_CALL(api, DeviceDestroy(serverDevice)).Times(0);
    EXPECT_CALL(api, DeviceRelease(serverDevice));
    FlushClient();
    EXPECT_FALSE(GetWireServer()->IsDeviceKnown(serverDevice));
}

// Test that deleting the client returns the server's reference to an imported device the client
// still holds, without destroying the device.
TEST_P(WireInjectDeviceTests, DeletingTheClientReleasesAnImportedDevice) {
    WGPUDevice serverDevice = api.GetNewDevice();
    wgpu::Device injected = ImportDevice(serverDevice);
    FlushClient();

    DeleteClient();
    // The device the wire requested is destroyed and released as ever.
    EXPECT_CALL(api, DeviceDestroy(apiDevice)).Times(AnyNumber());
    EXPECT_CALL(api, DeviceRelease(apiDevice));
    EXPECT_CALL(api, DeviceDestroy(serverDevice)).Times(0);
    EXPECT_CALL(api, DeviceRelease(serverDevice));
    FlushClient();
    EXPECT_FALSE(GetWireServer()->IsDeviceKnown(serverDevice));
}

// Test that the server's destruction returns its reference to an imported device without
// destroying the device.
TEST_P(WireInjectDeviceTests, DeletingTheServerKeepsAnImportedDevice) {
    WGPUDevice serverDevice = api.GetNewDevice();
    wgpu::Device injected = ImportDevice(serverDevice);
    FlushClient();
    Handle handle = GetWireClient()->GetWireHandle(injected.Get());
    ASSERT_EQ(GetWireServer()->GetDevice(handle.id, handle.generation), serverDevice);

    // The device the wire requested is destroyed and released as ever.
    EXPECT_CALL(api, DeviceDestroy(apiDevice)).Times(AnyNumber());
    EXPECT_CALL(api, DeviceRelease(apiDevice));
    EXPECT_CALL(api, DeviceDestroy(serverDevice)).Times(0);
    EXPECT_CALL(api, DeviceRelease(serverDevice)).Times(1);
    DeleteServer();
    Mock::VerifyAndClearExpectations(&api);
}

// Test that the client's explicit Destroy() still destroys an imported device.
TEST_P(WireInjectDeviceTests, DestroyDestroysAnImportedDevice) {
    WGPUDevice serverDevice = api.GetNewDevice();
    wgpu::Device injected = ImportDevice(serverDevice);

    EXPECT_CALL(mDeviceLostCb,
                Call(Pointee(NotNull()), WGPUDeviceLostReason_Destroyed, _, nullptr, this));
    injected.Destroy();
    Mock::VerifyAndClearExpectations(&mDeviceLostCb);

    EXPECT_CALL(api, DeviceDestroy(serverDevice)).Times(1);
    FlushClient();
}

}  // anonymous namespace
}  // namespace dawn::wire
