// Where the embedder's swapchain wrapper is kept. The five entry points are handed to it with the driver's
// own already in them, so a wrapper cannot be installed without being given the thing it stands in front of.

#ifndef SRC_DAWN_NATIVE_VULKAN_SWAPCHAINHOOKSVK_H_
#define SRC_DAWN_NATIVE_VULKAN_SWAPCHAINHOOKSVK_H_

#include "dawn/native/VulkanBackend.h"

namespace dawn::native::vulkan {

// Inline so the storage is one per component with no source-list change; registration happens on the
// embedder's thread before the device is created, and is read once as its procs are loaded.
inline VulkanSwapchainInterposer& MutableSwapchainInterposer() {
    static VulkanSwapchainInterposer hook = nullptr;
    return hook;
}

}  // namespace dawn::native::vulkan

#endif  // SRC_DAWN_NATIVE_VULKAN_SWAPCHAINHOOKSVK_H_
