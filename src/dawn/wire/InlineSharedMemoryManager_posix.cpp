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

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "src/dawn/common/Math.h"
#include "src/dawn/common/Ref.h"
#include "src/dawn/common/SystemHandle.h"
#include "src/dawn/wire/InlineSharedMemoryManager.h"
#include "src/utils/numeric.h"
#include "src/utils/span.h"

namespace dawn::wire {

namespace {

// The manager hands one mapping to a client and a server in the same process, so an anonymous
// private mapping is the whole transfer and no descriptor ever crosses a process boundary.
class SharedMemoryPosix : public SharedMemory {
  public:
    SharedMemoryPosix(std::span<std::byte> data, uint64_t allocatedSize)
        : SharedMemory(SystemHandle(), data, allocatedSize) {}

    ~SharedMemoryPosix() override {
        std::span<std::byte> data = GetMappedSpan();
        if (data.data() != nullptr) {
            munmap(data.data(), checked_cast<size_t>(GetAllocatedSize()));
        }
    }
};

class InlineSharedMemoryManagerImpl_Posix : public InlineSharedMemoryManager {
  public:
    Ref<SharedMemory> CreateSharedMemory(size_t size) override {
        const uint64_t alignedSize =
            Align(static_cast<uint64_t>(size), static_cast<size_t>(sysconf(_SC_PAGESIZE)));

        // Anonymous pages arrive zero-filled, which `CreateSharedMemory` promises.
        void* pointer = mmap(nullptr, checked_cast<size_t>(alignedSize), PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pointer == MAP_FAILED) {
            return nullptr;
        }

        // SAFETY: the pointer returned by mmap points to at least `size` valid bytes.
        auto data = DAWN_UNSAFE_BUFFERS(Span<std::byte>{static_cast<std::byte*>(pointer), size});
        return AcquireRef(new SharedMemoryPosix(data, alignedSize));
    }
};

}  // namespace

std::shared_ptr<InlineSharedMemoryManager> CreateInlineSharedMemoryManager() {
    return std::make_shared<InlineSharedMemoryManagerImpl_Posix>();
}

}  // namespace dawn::wire
