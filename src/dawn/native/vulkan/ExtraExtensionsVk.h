// Extension names the embedder asks for on top of Dawn's own. A vendor SDK that runs on Dawn's device
// -- DLSS through NGX, say -- needs its extensions on the instance and the device Dawn creates.

#ifndef SRC_DAWN_NATIVE_VULKAN_EXTRAEXTENSIONSVK_H_
#define SRC_DAWN_NATIVE_VULKAN_EXTRAEXTENSIONSVK_H_

#include <string>
#include <vector>

#include "src/dawn/common/vulkan_platform.h"

namespace dawn::native::vulkan {

// Inline so the storage is one per component with no source-list change; registration happens on the
// embedder's thread before anything is created, and is read once at creation.
inline std::vector<std::string>& MutableExtraInstanceExtensions() {
    static std::vector<std::string> names;
    return names;
}

inline std::vector<std::string>& MutableExtraDeviceExtensions() {
    static std::vector<std::string> names;
    return names;
}

// The loader to open instead of the platform's own. An interposer -- Streamline, for DLSS frame generation
// -- has to sit under vkCreateInstance and vkCreateDevice to add the queues and hooks its feature needs.
inline std::string& MutableVulkanLoader() {
    static std::string name;
    return name;
}

// Appends the wanted names the driver actually advertises and Dawn has not already asked for. A name
// no driver offers is dropped rather than refused: the embedder loses the feature, the device still boots.
inline void AppendExtraExtensions(const std::vector<std::string>& wanted,
                                  const std::vector<VkExtensionProperties>& advertised,
                                  std::vector<const char*>* names) {
    for (const std::string& want : wanted) {
        bool offered = false;
        for (const VkExtensionProperties& has : advertised) {
            if (want == has.extensionName) {
                offered = true;
                break;
            }
        }
        if (!offered) {
            continue;
        }
        bool already = false;
        for (const char* named : *names) {
            if (want == named) {
                already = true;
                break;
            }
        }
        if (!already) {
            names->push_back(want.c_str());
        }
    }
}

}  // namespace dawn::native::vulkan

#endif  // SRC_DAWN_NATIVE_VULKAN_EXTRAEXTENSIONSVK_H_
