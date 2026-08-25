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

// VulkanBackend.cpp: contains the definition of symbols exported by VulkanBackend.h so that they
// can be compiled twice: once export (shared library), once not exported (static library)

#include <utility>

// Include vulkan_platform.h before VulkanBackend.h includes vulkan.h so that we use our version
// of the non-dispatchable handles.
#include "src/dawn/common/vulkan_platform.h"

// Must be after vulkan_platform
#include "dawn/native/VulkanBackend.h"
#include "src/dawn/native/Adapter.h"
#include "src/dawn/native/PhysicalDevice.h"
#include "src/dawn/native/vulkan/BackendVk.h"
#include "src/dawn/native/vulkan/CommandRecordingContextVk.h"
#include "src/dawn/native/vulkan/DeviceVk.h"
#include "src/dawn/native/vulkan/ExtraExtensionsVk.h"
#include "src/dawn/native/vulkan/ExtraQueuesVk.h"
#include "src/dawn/native/vulkan/PhysicalDeviceVk.h"
#include "src/dawn/native/vulkan/QueueVk.h"
#include "src/dawn/native/vulkan/SwapchainHooksVk.h"
#include "src/dawn/native/vulkan/TextureVk.h"

namespace dawn::native::vulkan {

namespace {

// ToBackend is an unchecked downcast, so everything below asks first: these are the entry points an
// app that can pick D3D12 or Vulkan at run time calls on whichever device it ended up with.
Device* AsVulkan(WGPUDevice device) {
    DeviceBase* base = FromAPI(device);
    if (base == nullptr ||
        base->GetPhysicalDevice()->GetBackendType() != wgpu::BackendType::Vulkan) {
        return nullptr;
    }
    return ToBackend(base);
}

PhysicalDevice* AsVulkan(WGPUAdapter adapter) {
    AdapterBase* base = FromAPI(adapter);
    if (base == nullptr ||
        base->GetPhysicalDevice()->GetBackendType() != wgpu::BackendType::Vulkan) {
        return nullptr;
    }
    return ToBackend(base->GetPhysicalDevice());
}

Texture* AsVulkan(WGPUTexture texture) {
    TextureBase* base = FromAPI(texture);
    if (base == nullptr ||
        base->GetDevice()->GetPhysicalDevice()->GetBackendType() != wgpu::BackendType::Vulkan) {
        return nullptr;
    }
    return ToBackend(base);
}

}  // namespace

VkInstance GetInstance(WGPUDevice device) {
    Device* backendDevice = ToBackend(FromAPI(device));
    return backendDevice->GetVkInstance();
}

VkDevice GetVkDevice(WGPUDevice device) {
    Device* backendDevice = AsVulkan(device);
    return backendDevice == nullptr ? VK_NULL_HANDLE : backendDevice->GetVkDevice();
}

VkQueue GetVkQueue(WGPUDevice device) {
    Device* backendDevice = AsVulkan(device);
    return backendDevice == nullptr ? VK_NULL_HANDLE
                                    : ToBackend(backendDevice->GetQueue())->GetVkQueue();
}

VkPhysicalDevice GetVkPhysicalDevice(WGPUDevice device) {
    Device* backendDevice = AsVulkan(device);
    if (backendDevice == nullptr) {
        return VK_NULL_HANDLE;
    }
    return ToBackend(backendDevice->GetPhysicalDevice())->GetVkPhysicalDevice();
}

uint32_t GetQueueFamilyIndex(WGPUDevice device) {
    Device* backendDevice = AsVulkan(device);
    return backendDevice == nullptr ? 0 : backendDevice->GetGraphicsQueueFamily();
}

VkInstance GetVkInstance(WGPUAdapter adapter) {
    PhysicalDevice* physical = AsVulkan(adapter);
    return physical == nullptr ? VK_NULL_HANDLE
                               : physical->GetVulkanInstance()->GetVkInstance();
}

VkPhysicalDevice GetVkPhysicalDevice(WGPUAdapter adapter) {
    PhysicalDevice* physical = AsVulkan(adapter);
    return physical == nullptr ? VK_NULL_HANDLE : physical->GetVkPhysicalDevice();
}

PFN_vkVoidFunction GetAdapterInstanceProcAddr(WGPUAdapter adapter, const char* pName) {
    PhysicalDevice* physical = AsVulkan(adapter);
    if (physical == nullptr) {
        return nullptr;
    }
    const VulkanFunctions& fn = physical->GetVulkanInstance()->GetFunctions();
    return (*fn.GetInstanceProcAddr)(physical->GetVulkanInstance()->GetVkInstance(), pName);
}

DAWN_NATIVE_EXPORT PFN_vkVoidFunction GetInstanceProcAddr(WGPUDevice device, const char* pName) {
    Device* backendDevice = ToBackend(FromAPI(device));
    return (*backendDevice->fn.GetInstanceProcAddr)(backendDevice->GetVkInstance(), pName);
}

// No EndBlit analogue is needed the way the Metal peer needs one: Vulkan render passes begin and end
// inside RecordCommands, so the pending buffer is always found recording with no pass open.
VkCommandBuffer GetPendingVkCommandBuffer(WGPUDevice device) {
    Device* backendDevice = AsVulkan(device);
    if (backendDevice == nullptr) {
        return VK_NULL_HANDLE;
    }
    auto deviceGuard = backendDevice->GetGuard();
    CommandRecordingContext* recordingContext =
        ToBackend(backendDevice->GetQueue())->GetPendingRecordingContext();
    return recordingContext->commandBuffer;
}

::VkImage GetVkImage(WGPUTexture texture) {
    Texture* backendTexture = AsVulkan(texture);
    return backendTexture == nullptr ? VK_NULL_HANDLE : backendTexture->GetHandle();
}

VkImageLayout GetVkImageLayout(WGPUTexture texture) {
    Texture* backendTexture = AsVulkan(texture);
    if (backendTexture == nullptr) {
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return backendTexture->GetCurrentLayout(backendTexture->GetDisjointVulkanAspects());
}

VkImageLayout GetVkImageLayoutForUsage(WGPUTexture texture, WGPUTextureUsage usage) {
    Texture* backendTexture = AsVulkan(texture);
    if (backendTexture == nullptr) {
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return backendTexture->VulkanImageLayout(static_cast<wgpu::TextureUsage>(usage));
}

void SetVkImageLayoutUsage(WGPUTexture texture, WGPUTextureUsage usage) {
    Texture* backendTexture = AsVulkan(texture);
    if (backendTexture == nullptr) {
        return;
    }
    backendTexture->UpdateUsage(static_cast<wgpu::TextureUsage>(usage), wgpu::ShaderStage::Compute,
                                backendTexture->GetAllSubresources());
}

void SetTextureInitialized(WGPUTexture texture) {
    Texture* backendTexture = AsVulkan(texture);
    if (backendTexture == nullptr) {
        return;
    }
    backendTexture->SetIsSubresourceContentInitialized(true, backendTexture->GetAllSubresources());
}

void RequestExtraInstanceExtensions(const char* const* names, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (names[i] != nullptr) {
            MutableExtraInstanceExtensions().push_back(names[i]);
        }
    }
}

void RequestExtraDeviceExtensions(const char* const* names, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (names[i] != nullptr) {
            MutableExtraDeviceExtensions().push_back(names[i]);
        }
    }
}

void RequestTimelineSemaphores() {
    MutableWantTimelineSemaphores() = true;
}

bool HasTimelineSemaphores() {
    return MutableGotTimelineSemaphores();
}

void RequestVulkanLoader(const char* libraryName) {
    MutableVulkanLoader() = libraryName != nullptr ? libraryName : "";
}

void RequestExtraQueues(uint32_t count) {
    MutableExtraQueueRequest() = count;
}

uint32_t GetExtraQueueCount() {
    return static_cast<uint32_t>(MutableExtraQueues().size());
}

VkQueue GetExtraQueue(uint32_t index) {
    const std::vector<ExtraQueue>& made = MutableExtraQueues();
    return index < made.size() ? made[index].queue : VK_NULL_HANDLE;
}

uint32_t GetExtraQueueFamily(uint32_t index) {
    const std::vector<ExtraQueue>& made = MutableExtraQueues();
    return index < made.size() ? made[index].family : 0;
}

void RequestSwapchainInterposer(VulkanSwapchainInterposer hook) {
    MutableSwapchainInterposer() = hook;
}

#if DAWN_PLATFORM_IS(LINUX)
ExternalImageDescriptorOpaqueFD::ExternalImageDescriptorOpaqueFD()
    : ExternalImageDescriptorFD(ExternalImageType::OpaqueFD) {}

ExternalImageDescriptorDmaBuf::ExternalImageDescriptorDmaBuf()
    : ExternalImageDescriptorFD(ExternalImageType::DmaBuf) {}

ExternalImageExportInfoOpaqueFD::ExternalImageExportInfoOpaqueFD()
    : ExternalImageExportInfoFD(ExternalImageType::OpaqueFD) {}

ExternalImageExportInfoDmaBuf::ExternalImageExportInfoDmaBuf()
    : ExternalImageExportInfoFD(ExternalImageType::DmaBuf) {}
#endif  // DAWN_PLATFORM_IS(LINUX)

#if DAWN_PLATFORM_IS(ANDROID)
ExternalImageDescriptorAHardwareBuffer::ExternalImageDescriptorAHardwareBuffer()
    : ExternalImageDescriptorVk(ExternalImageType::AHardwareBuffer) {}

ExternalImageExportInfoAHardwareBuffer::ExternalImageExportInfoAHardwareBuffer()
    : ExternalImageExportInfoFD(ExternalImageType::AHardwareBuffer) {}
#endif

WGPUTexture WrapVulkanImage(WGPUDevice device, const ExternalImageDescriptorVk* descriptor) {
    Device* backendDevice = ToBackend(FromAPI(device));
    auto deviceGuard = backendDevice->GetGuard();
    switch (descriptor->GetType()) {
#if DAWN_PLATFORM_IS(ANDROID)
        case ExternalImageType::AHardwareBuffer: {
            const ExternalImageDescriptorAHardwareBuffer* ahbDescriptor =
                static_cast<const ExternalImageDescriptorAHardwareBuffer*>(descriptor);
            Ref<TextureBase> texture = backendDevice->CreateTextureWrappingVulkanImage(
                ahbDescriptor, ahbDescriptor->handle, ahbDescriptor->waitFDs);
            return ToAPI(ReturnToAPI(std::move(texture)));
        }
#elif DAWN_PLATFORM_IS(LINUX)
        case ExternalImageType::OpaqueFD:
        case ExternalImageType::DmaBuf: {
            const ExternalImageDescriptorFD* fdDescriptor =
                static_cast<const ExternalImageDescriptorFD*>(descriptor);
            Ref<TextureBase> texture = backendDevice->CreateTextureWrappingVulkanImage(
                fdDescriptor, fdDescriptor->memoryFD, fdDescriptor->waitFDs);
            return ToAPI(ReturnToAPI(std::move(texture)));
        }
#endif  // DAWN_PLATFORM_IS(LINUX)

        default:
            return nullptr;
    }
}

bool ExportVulkanImage(WGPUTexture texture,
                       VkImageLayout desiredLayout,
                       ExternalImageExportInfoVk* info) {
    if (texture == nullptr) {
        return false;
    }
    Texture* backendTexture = ToBackend(FromAPI(texture));
    Device* device = ToBackend(backendTexture->GetDevice());
    auto deviceGuard = device->GetGuard();
#if DAWN_PLATFORM_IS(ANDROID) || DAWN_PLATFORM_IS(LINUX)
    switch (info->GetType()) {
        case ExternalImageType::AHardwareBuffer:
        case ExternalImageType::OpaqueFD:
        case ExternalImageType::DmaBuf: {
            ExternalImageExportInfoFD* fdInfo = static_cast<ExternalImageExportInfoFD*>(info);

            return device->SignalAndExportExternalTexture(backendTexture, desiredLayout, fdInfo,
                                                          &fdInfo->semaphoreHandles);
        }
        default:
            return false;
    }
#else
    return false;
#endif  // DAWN_PLATFORM_IS(LINUX)
}

bool ExportVulkanImage(WGPUTexture texture, ExternalImageExportInfoVk* info) {
    return ExportVulkanImage(texture, VK_IMAGE_LAYOUT_UNDEFINED, info);
}

}  // namespace dawn::native::vulkan
