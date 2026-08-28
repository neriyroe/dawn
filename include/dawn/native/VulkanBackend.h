// Copyright 2018 The Dawn & Tint Authors
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

#ifndef INCLUDE_DAWN_NATIVE_VULKANBACKEND_H_
#define INCLUDE_DAWN_NATIVE_VULKANBACKEND_H_

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <vector>

#include "dawn/native/DawnNative.h"

namespace dawn::native::vulkan {

DAWN_NATIVE_EXPORT VkInstance GetInstance(WGPUDevice device);

DAWN_NATIVE_EXPORT PFN_vkVoidFunction GetInstanceProcAddr(WGPUDevice device, const char* pName);

// The handles behind a Dawn device, for an SDK that runs its own passes on it. Each answers null on a
// device from another backend, so an app that chooses its backend at run time may ask without checking.
DAWN_NATIVE_EXPORT VkDevice GetVkDevice(WGPUDevice device);
DAWN_NATIVE_EXPORT VkQueue GetVkQueue(WGPUDevice device);
DAWN_NATIVE_EXPORT VkPhysicalDevice GetVkPhysicalDevice(WGPUDevice device);
DAWN_NATIVE_EXPORT uint32_t GetQueueFamilyIndex(WGPUDevice device);

// The same, from the adapter -- which is what an SDK needs to answer for its own extension
// requirements, since the device that must enable them does not exist yet.
DAWN_NATIVE_EXPORT VkInstance GetVkInstance(WGPUAdapter adapter);
DAWN_NATIVE_EXPORT VkPhysicalDevice GetVkPhysicalDevice(WGPUAdapter adapter);
DAWN_NATIVE_EXPORT PFN_vkVoidFunction GetAdapterInstanceProcAddr(WGPUAdapter adapter,
                                                                 const char* pName);

// Dawn's own open command buffer, to encode into until its next submit commits it. One queue and one
// command stream is what lets an outside pass order itself against the renderer with ordinary barriers.
DAWN_NATIVE_EXPORT VkCommandBuffer GetPendingVkCommandBuffer(WGPUDevice device);

// The image behind a wgpu texture, and the layout Dawn believes each of its subresources is in.
DAWN_NATIVE_EXPORT ::VkImage GetVkImage(WGPUTexture texture);
DAWN_NATIVE_EXPORT VkImageLayout GetVkImageLayout(WGPUTexture texture);
// The layout Dawn would put the texture in for that usage. Asked rather than assumed: the answer
// depends on the whole usage set the texture was created with, not on `usage` alone.
DAWN_NATIVE_EXPORT VkImageLayout GetVkImageLayoutForUsage(WGPUTexture texture,
                                                          WGPUTextureUsage usage);
// Tells Dawn about a layout an outside barrier already moved the image to, so its next transition
// starts from the truth rather than from what it last recorded.
DAWN_NATIVE_EXPORT void SetVkImageLayoutUsage(WGPUTexture texture, WGPUTextureUsage usage);
// An image written outside Dawn's sight is not uninitialised, whatever its lazy-clear bookkeeping
// believes -- and a lazy clear would land on top of the very output the pass exists to produce.
DAWN_NATIVE_EXPORT void SetTextureInitialized(WGPUTexture texture);

// Extension names to enable on the instance and on the device Dawn creates, registered before either
// exists. A name the driver does not advertise is dropped rather than refused, so a machine without
// the vendor's extensions still boots and only loses the feature that wanted them.
DAWN_NATIVE_EXPORT void RequestExtraInstanceExtensions(const char* const* names, size_t count);
DAWN_NATIVE_EXPORT void RequestExtraDeviceExtensions(const char* const* names, size_t count);

// Timeline semaphores, which the extension alone does not switch on -- a feature promoted to core is inert
// until it is asked for by name. Granted only where the driver advertises it, because a feature the device
// does not have fails device creation and takes the renderer with it: ask, then read back what was given.
DAWN_NATIVE_EXPORT void RequestTimelineSemaphores();
DAWN_NATIVE_EXPORT bool HasTimelineSemaphores();

// Ray query, the same ask-then-read-back. Dawn traces nothing itself: this switches on acceleration
// structures, the query, and the buffer device addresses their builds are fed through, for an embedder
// that records ray tracing into Dawn's own command buffer.
DAWN_NATIVE_EXPORT void RequestRayQuery();
DAWN_NATIVE_EXPORT bool HasRayQuery();

// Device features an embedder's SDK needs that Dawn has no use for itself. Asked for by name rather than
// as a chain: an SDK states these in the aggregate VkPhysicalDeviceVulkan1x structs, and Vulkan forbids
// those sharing a pNext chain with the individual promoted ones Dawn already builds its device from.
struct VulkanExtraFeatures {
    bool shaderInt8 = false;
    bool scalarBlockLayout = false;
    bool mutableDescriptorType = false;
};

// The same ask-then-read-back as the two above: each is granted only where the driver both enables the
// extension carrying it and advertises the feature, because one it does not fails device creation outright.
DAWN_NATIVE_EXPORT void RequestExtraDeviceFeatures(const VulkanExtraFeatures& wanted);
DAWN_NATIVE_EXPORT VulkanExtraFeatures GetExtraDeviceFeatures();

// Dawn's own place in the queue's timeline, for an embedder whose resources a frame in flight still names.
// The pending serial is the one the work being recorded now will be submitted under; the completed one is
// the last that has retired, so anything stamped at or below it is nobody's any more. Zero off Vulkan.
DAWN_NATIVE_EXPORT uint64_t GetPendingCommandSerial(WGPUDevice device);
DAWN_NATIVE_EXPORT uint64_t GetCompletedCommandSerial(WGPUDevice device);
// Blocks until that serial retires, or the timeout passes. False when it did not, which is the only answer
// a caller about to reuse the memory behind it may act on.
DAWN_NATIVE_EXPORT bool WaitForCommandSerial(WGPUDevice device, uint64_t serial, uint64_t timeoutNs);

// A loader to open in place of the platform's own, registered before the instance exists. An interposer
// has to sit under vkCreateInstance and vkCreateDevice to add queues and hooks of its own; one that will
// not open is skipped, and the platform loader brings the backend up without it.
DAWN_NATIVE_EXPORT void RequestVulkanLoader(const char* libraryName);

// Queues beyond the one universal queue Dawn creates for itself, registered before the device exists. A
// runtime that presents on our behalf submits from queues of its own and will not share the renderer's.
// The count granted may be smaller than the one asked for, on a card with no more queues to give.
DAWN_NATIVE_EXPORT void RequestExtraQueues(uint32_t count);
DAWN_NATIVE_EXPORT uint32_t GetExtraQueueCount();
DAWN_NATIVE_EXPORT VkQueue GetExtraQueue(uint32_t index);
DAWN_NATIVE_EXPORT uint32_t GetExtraQueueFamily(uint32_t index);

// The swapchain entry points Dawn drives its surface through, offered to an embedder that stands in front
// of them -- a frame generator presenting more frames than the renderer drew. The struct arrives holding
// the driver's own, so whatever replaces them keeps a way to fall through for the frames it does not touch.
struct VulkanSwapchainProcs {
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
};

using VulkanSwapchainInterposer = void (*)(VulkanSwapchainProcs* procs);

DAWN_NATIVE_EXPORT void RequestSwapchainInterposer(VulkanSwapchainInterposer hook);

enum class NeedsDedicatedAllocation {
    Yes,
    No,
    // Use Vulkan reflection to detect whether a dedicated allocation is needed.
    Detect,
};

struct DAWN_NATIVE_EXPORT ExternalImageDescriptorVk : ExternalImageDescriptor {
  public:
    // The following members may be ignored if |ExternalImageDescriptor::isInitialized| is false
    // since the import does not need to preserve texture contents.

    // See https://www.khronos.org/registry/vulkan/specs/1.1/html/chap7.html. The acquire
    // operation old/new layouts must match exactly the layouts in the release operation. So
    // we may need to issue two barriers releasedOldLayout -> releasedNewLayout ->
    // cTextureDescriptor.usage if the new layout is not compatible with the desired usage.
    // The first barrier is the queue transfer, the second is the layout transition to our
    // desired usage.
    VkImageLayout releasedOldLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkImageLayout releasedNewLayout = VK_IMAGE_LAYOUT_GENERAL;

    // Try to detect the need to use a dedicated allocation for imported images by default but let
    // the application override this as drivers have bugs and forget to require a dedicated
    // allocation.
    NeedsDedicatedAllocation dedicatedAllocation = NeedsDedicatedAllocation::Detect;

  protected:
    using ExternalImageDescriptor::ExternalImageDescriptor;
};

struct ExternalImageExportInfoVk : ExternalImageExportInfo {
  public:
    // See comments in |ExternalImageDescriptorVk|
    // Contains the old/new layouts used in the queue release operation.
    VkImageLayout releasedOldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout releasedNewLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  protected:
    using ExternalImageExportInfo::ExternalImageExportInfo;
};

// Can't use DAWN_PLATFORM_IS(LINUX) since header included in both Dawn and Chrome
#if defined(__linux__) || defined(__Fuchsia__)

// Common properties of external images represented by FDs. On successful import the file
// descriptor's ownership is transferred to the Dawn implementation and they shouldn't be
// used outside of Dawn again. TODO(enga): Also transfer ownership in the error case so the
// caller can assume the FD is always consumed.
struct DAWN_NATIVE_EXPORT ExternalImageDescriptorFD : ExternalImageDescriptorVk {
  public:
    int memoryFD = -1;         // A file descriptor from an export of the memory of the image
    std::vector<int> waitFDs;  // File descriptors of semaphores which will be waited on

  protected:
    using ExternalImageDescriptorVk::ExternalImageDescriptorVk;
};

// Descriptor for opaque file descriptor image import
struct DAWN_NATIVE_EXPORT ExternalImageDescriptorOpaqueFD : ExternalImageDescriptorFD {
    ExternalImageDescriptorOpaqueFD();

    VkDeviceSize allocationSize = 0;  // Must match VkMemoryAllocateInfo from image creation
    uint32_t memoryTypeIndex = 0;     // Must match VkMemoryAllocateInfo from image creation
};

// The plane-wise offset and stride.
struct DAWN_NATIVE_EXPORT PlaneLayout {
    uint64_t offset = 0;
    uint32_t stride = 0;
};

// Descriptor for dma-buf file descriptor image import
struct DAWN_NATIVE_EXPORT ExternalImageDescriptorDmaBuf : ExternalImageDescriptorFD {
    ExternalImageDescriptorDmaBuf();

    static constexpr uint32_t kMaxPlanes = 3;
    std::array<PlaneLayout, kMaxPlanes> planeLayouts = {};
    uint64_t drmModifier = 0;  // DRM modifier of the buffer
};

// Info struct that is written to in |ExportVulkanImage|.
struct DAWN_NATIVE_EXPORT ExternalImageExportInfoFD : ExternalImageExportInfoVk {
  public:
    // Contains the exported semaphore handles.
    std::vector<int> semaphoreHandles;

  protected:
    using ExternalImageExportInfoVk::ExternalImageExportInfoVk;
};

struct DAWN_NATIVE_EXPORT ExternalImageExportInfoOpaqueFD : ExternalImageExportInfoFD {
    ExternalImageExportInfoOpaqueFD();
};

struct DAWN_NATIVE_EXPORT ExternalImageExportInfoDmaBuf : ExternalImageExportInfoFD {
    ExternalImageExportInfoDmaBuf();
};

#ifdef __ANDROID__

// Descriptor for AHardwareBuffer image import
struct DAWN_NATIVE_EXPORT ExternalImageDescriptorAHardwareBuffer : ExternalImageDescriptorVk {
  public:
    ExternalImageDescriptorAHardwareBuffer();

    struct AHardwareBuffer* handle;  // The AHardwareBuffer which contains the memory of the image
    std::vector<int> waitFDs;        // File descriptors of semaphores which will be waited on

  protected:
    using ExternalImageDescriptorVk::ExternalImageDescriptorVk;
};

struct DAWN_NATIVE_EXPORT ExternalImageExportInfoAHardwareBuffer : ExternalImageExportInfoFD {
    ExternalImageExportInfoAHardwareBuffer();
};

#endif  // __ANDROID__

#endif  // defined(__linux__) || defined(__Fuchsia__)

// Imports external memory into a Vulkan image. Internally, this uses external memory /
// semaphore extensions to import the image and wait on the provided synchronizaton
// primitives before the texture can be used.
// On failure, returns a nullptr.
DAWN_NATIVE_EXPORT WGPUTexture WrapVulkanImage(WGPUDevice device,
                                               const ExternalImageDescriptorVk* descriptor);

// Exports external memory from a Vulkan image. This must be called on wrapped textures
// before they are destroyed. It writes the semaphore to wait on and the old/new image
// layouts to |info|. Pass VK_IMAGE_LAYOUT_UNDEFINED as |desiredLayout| if you don't want to
// perform a layout transition.
DAWN_NATIVE_EXPORT bool ExportVulkanImage(WGPUTexture texture,
                                          VkImageLayout desiredLayout,
                                          ExternalImageExportInfoVk* info);
// |ExportVulkanImage| with default desiredLayout of VK_IMAGE_LAYOUT_UNDEFINED.
DAWN_NATIVE_EXPORT bool ExportVulkanImage(WGPUTexture texture, ExternalImageExportInfoVk* info);

}  // namespace dawn::native::vulkan

#endif  // INCLUDE_DAWN_NATIVE_VULKANBACKEND_H_
