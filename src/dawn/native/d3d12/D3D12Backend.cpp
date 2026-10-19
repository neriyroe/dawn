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

// D3D12Backend.cpp: contains the definition of symbols exported by D3D12Backend.h so that they
// can be compiled twice: once export (shared library), once not exported (static library)

#include "dawn/native/D3D12Backend.h"

#include <memory>
#include <utility>

#include "src/dawn/common/Math.h"
#include "src/dawn/native/PhysicalDevice.h"
#include "src/dawn/native/Surface.h"
#include "src/dawn/native/d3d12/DeviceD3D12.h"
#include "src/dawn/native/d3d12/QueueD3D12.h"
#include "src/dawn/native/d3d12/ResidencyManagerD3D12.h"
#include "src/dawn/native/d3d12/SwapChainD3D12.h"
#include "src/dawn/native/d3d12/TextureD3D12.h"
#include "src/utils/log.h"

namespace dawn::native::d3d12 {

namespace {

// ToBackend below is an unchecked downcast, so a device from another backend would be read as a
// D3D12 one. An app that can pick its backend at run time asks these on every boot.
bool IsD3D12(DeviceBase* device) {
    return device != nullptr &&
           device->GetPhysicalDevice()->GetBackendType() == wgpu::BackendType::D3D12;
}

Texture* AsD3D12(WGPUTexture texture) {
    TextureBase* base = FromAPI(texture);
    return base != nullptr && !base->IsError() && IsD3D12(base->GetDevice())
               ? ToBackend(base)
               : nullptr;
}

}  // namespace

Microsoft::WRL::ComPtr<ID3D11On12Device> GetOrCreateD3D11On12Device(WGPUDevice device) {
    DeviceBase* base = FromAPI(device);
    if (!IsD3D12(base)) {
        return nullptr;
    }
    return ToBackend(base)->GetOrCreateD3D11On12Device();
}

Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue(WGPUDevice device) {
    DeviceBase* base = FromAPI(device);
    if (!IsD3D12(base)) {
        return nullptr;
    }
    return ToBackend(base)->GetD3D12CommandQueue();
}

Microsoft::WRL::ComPtr<ID3D12Device> GetD3D12Device(WGPUDevice device) {
    DeviceBase* base = FromAPI(device);
    if (!IsD3D12(base)) {
        return nullptr;
    }
    return ToBackend(base)->GetD3D12Device();
}

ID3D12GraphicsCommandList* GetPendingD3D12CommandList(WGPUDevice device) {
    DeviceBase* base = FromAPI(device);
    if (!IsD3D12(base)) {
        return nullptr;
    }
    auto deviceGuard = base->GetGuard();
    return ToBackend(base->GetQueue())->GetPendingCommandContext()->GetCommandList();
}

ID3D12Resource* GetD3D12Resource(WGPUTexture texture) {
    Texture* backendTexture = AsD3D12(texture);
    return backendTexture != nullptr ? backendTexture->GetD3D12Resource() : nullptr;
}

bool KeepTextureResident(WGPUTexture texture) {
    Texture* backendTexture = AsD3D12(texture);
    if (backendTexture == nullptr || backendTexture->GetD3D12Resource() == nullptr) {
        return false;
    }
    Device* device = ToBackend(backendTexture->GetDevice());
    auto deviceGuard = device->GetGuard();
    return !device->ConsumedError(backendTexture->KeepResident());
}

void TransitionD3D12TextureState(WGPUTexture texture, D3D12_RESOURCE_STATES state) {
    Texture* backendTexture = AsD3D12(texture);
    if (backendTexture == nullptr || backendTexture->GetD3D12Resource() == nullptr) {
        return;
    }
    DeviceBase* device = backendTexture->GetDevice();
    auto deviceGuard = device->GetGuard();
    auto* context = ToBackend(device->GetQueue())->GetPendingCommandContext();
    backendTexture->TrackUsageAndTransitionNow(context, state, backendTexture->GetAllSubresources());
}

void SetTextureInitialized(WGPUTexture texture) {
    Texture* backendTexture = AsD3D12(texture);
    if (backendTexture == nullptr) {
        return;
    }
    auto deviceGuard = backendTexture->GetDevice()->GetGuard();
    backendTexture->SetIsSubresourceContentInitialized(true, backendTexture->GetAllSubresources());
}

uint64_t GetPendingCommandSerial(WGPUDevice device) {
    DeviceBase* base = FromAPI(device);
    return IsD3D12(base) ? static_cast<uint64_t>(base->GetQueue()->GetPendingCommandSerial()) : 0;
}

uint64_t GetCompletedCommandSerial(WGPUDevice device) {
    DeviceBase* base = FromAPI(device);
    return IsD3D12(base) ? static_cast<uint64_t>(base->GetQueue()->GetCompletedCommandSerial()) : 0;
}

bool UseGPUUploadHeap(WGPUDevice device, uint64_t allocationSize) {
    DeviceBase* base = FromAPI(device);
    if (!IsD3D12(base) || !ToBackend(base)->GetDeviceInfo().supportsGPUUploadHeap ||
        !base->IsToggleEnabled(Toggle::D3D12UseGPUUploadHeap)) {
        return false;
    }
    if (allocationSize == 0) {
        return true;
    }
    auto deviceGuard = base->GetGuard();
    return ToBackend(base)->GetResidencyManager()->CanAllocateWithoutEviction(allocationSize,
                                                                              MemorySegment::Local);
}

// Lets the app take the present queue's wait at the top of its frame instead of inside
// wgpuSurfaceGetCurrentTexture.
HANDLE GetFrameLatencyWaitableObject(WGPUSurface surface) {
    SwapChainBase* swapChain = FromAPI(surface)->GetCurrentSwapChain();
    if (swapChain == nullptr || swapChain->GetBackendType() != wgpu::BackendType::D3D12) {
        return nullptr;
    }
    return ToBackend(swapChain)->GetFrameLatencyWaitableObject();
}

uint64_t SetExternalMemoryReservation(WGPUDevice device,
                                      uint64_t requestedReservationSize,
                                      MemorySegment memorySegment) {
    Device* backendDevice = ToBackend(FromAPI(device));

    auto deviceGuard = backendDevice->GetGuard();

    return backendDevice->GetResidencyManager()->SetExternalMemoryReservation(
        memorySegment, requestedReservationSize);
}

}  // namespace dawn::native::d3d12
