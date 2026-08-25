// Queues an embedder asks for beyond the single universal one Dawn creates. A runtime that presents for us
// -- FidelityFX's interpolating swapchain -- submits from four VkQueues and refuses two that are the same.

#ifndef SRC_DAWN_NATIVE_VULKAN_EXTRAQUEUESVK_H_
#define SRC_DAWN_NATIVE_VULKAN_EXTRAQUEUESVK_H_

#include <algorithm>
#include <vector>

#include "src/dawn/common/vulkan_platform.h"

namespace dawn::native::vulkan {

// A slot in some family, planned before the device exists and resolved to a handle once it does.
struct ExtraQueue {
    uint32_t family = 0;
    uint32_t index = 0;
    VkQueue queue = VK_NULL_HANDLE;
};

// Inline so the storage is one per component with no source-list change; registration happens on the
// embedder's thread before anything is created, and is read once at creation.
inline uint32_t& MutableExtraQueueRequest() {
    static uint32_t wanted = 0;
    return wanted;
}

inline std::vector<ExtraQueue>& MutableExtraQueues() {
    static std::vector<ExtraQueue> planned;
    return planned;
}

// Spends the main family's spare slots first, then any other family, compute-capable ones before the rest.
// Fewer than asked for is the honest answer on a card with no more to give; the embedder reads the count.
inline std::vector<ExtraQueue> ChooseExtraQueues(const std::vector<VkQueueFamilyProperties>& families,
                                                 uint32_t mainFamily,
                                                 uint32_t wanted) {
    std::vector<ExtraQueue> chosen;
    if (wanted == 0 || families.empty() || mainFamily >= families.size()) {
        return chosen;
    }

    // Dawn's own queue already holds slot zero of the main family.
    std::vector<uint32_t> taken(families.size(), 0);
    taken[mainFamily] = 1;

    const auto claim = [&](uint32_t family) {
        if (taken[family] >= families[family].queueCount) {
            return false;
        }
        chosen.push_back({family, taken[family], VK_NULL_HANDLE});
        taken[family]++;
        return true;
    };

    const uint32_t none = static_cast<uint32_t>(families.size());
    while (chosen.size() < wanted) {
        if (claim(mainFamily)) {
            continue;
        }
        uint32_t compute = none;
        uint32_t other = none;
        for (uint32_t i = 0; i < families.size(); ++i) {
            if (i == mainFamily || taken[i] >= families[i].queueCount) {
                continue;
            }
            const bool computes = (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
            if (computes && compute == none) {
                compute = i;
            } else if (!computes && other == none) {
                other = i;
            }
        }
        const uint32_t pick = compute != none ? compute : other;
        if (pick == none || !claim(pick)) {
            break;
        }
    }
    return chosen;
}

}  // namespace dawn::native::vulkan

#endif  // SRC_DAWN_NATIVE_VULKAN_EXTRAQUEUESVK_H_
