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

#ifndef SRC_DAWN_NATIVE_DYNAMICUPLOADER_H_
#define SRC_DAWN_NATIVE_DYNAMICUPLOADER_H_

#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include "partition_alloc/pointers/raw_ptr.h"
#include "src/dawn/common/Ref.h"
#include "src/dawn/common/SerialQueue.h"
#include "src/dawn/native/Error.h"
#include "src/dawn/native/Forward.h"
#include "src/dawn/native/IntegerTypes.h"
#include "src/dawn/native/RingBufferAllocator.h"
#include "src/utils/non_movable.h"
#include "src/utils/span.h"

// DynamicUploader is the front-end implementation used to manage multiple ring buffers for upload
// usage.
namespace dawn::native {

class BufferBase;

struct UploadReservation {
    Span<std::byte> mappedData = {};
    uint64_t offsetInBuffer = 0;
    Ref<BufferBase> buffer;
};

class DynamicUploader : NonMovable {
  public:
    explicit DynamicUploader(DeviceBase* device);
    ~DynamicUploader() = default;

    // Transiently makes a reservation for an upload area for the functor passed in argument.
    template <typename F>
    MaybeError WithUploadReservation(uint64_t size, uint64_t offsetAlignment, F&& f) {
        // TODO(crbug.com/448168642): Assert that pending command serial doesn't change.
        UploadReservation reservation;
        DAWN_TRY_ASSIGN(reservation, Reserve(size, offsetAlignment));
        DAWN_TRY(f(reservation));
        return OnStagingMemoryFreePendingOnSubmit(size);
    }

    // Notifies the dynamic uploader that some freeing of memory is associated with the pending
    // submit. The dynamic uploader tracks this info, and if enough memory is freed before the
    // next submit, MaybeSubmitPendingCommands will submit early.
    MaybeError OnStagingMemoryFreePendingOnSubmit(uint64_t size);

    // May submit pending commands on the queue if enough memory was freed since the last submit.
    // This will lock the device mutex if a queue submit is necessary (and mutex exists).
    MaybeError MaybeSubmitPendingCommands();

    void Deallocate(ExecutionSerial lastCompletedSerial, bool freeAll = false);

    // Staging bytes held now, the idle pooled share of them, and the most of each since the last free-all.
    struct StagingMemory {
        uint64_t bytes = 0;
        uint64_t idleBytes = 0;
        uint64_t peakBytes = 0;
        uint64_t peakIdleBytes = 0;
    };
    StagingMemory GetStagingMemory() const;

  private:
    ResultOrError<UploadReservation> Reserve(uint64_t size, uint64_t offsetAlignment);

    // A mapped staging buffer of exactly `size` bytes: an idle one of that size, else a new one.
    ResultOrError<Ref<BufferBase>> AcquireStaging(uint64_t size);
    // Keeps a staging buffer no copy still reads for reuse, while the idle ones stay within their cap.
    void Recycle(Ref<BufferBase> buffer);

    // Checks if a submit happened and resets memory to be freed pending submit if so.
    void UpdateMemoryPendingSubmit();

    struct RingBuffer {
        Ref<BufferBase> mStagingBuffer;
        RingBufferAllocator mAllocator;
    };
    std::vector<std::unique_ptr<RingBuffer>> mRingBuffers;

    // Whole staging buffers of uploads too large for a ring, until the serial of their copy completes.
    SerialQueue<ExecutionSerial, Ref<BufferBase>> mStagingInFlight;
    // Idle staging buffers by size, still mapped, until a reuse or a load boundary's ReduceMemoryUsage.
    std::map<uint64_t, std::vector<Ref<BufferBase>>> mIdleStaging;
    uint64_t mIdleStagingBytes = 0;
    // Every staging byte the rings, the copies in flight and the idle pool hold, and the peaks reported.
    uint64_t mStagingBytes = 0;
    uint64_t mPeakStagingBytes = 0;
    uint64_t mPeakIdleStagingBytes = 0;

    // Serial used to track when a serial has been scheduled and the corresponding pending memory
    // will be freed in finite time.
    ExecutionSerial mLastPendingSerialSeen = kBeginningOfGPUTime;
    std::atomic<uint64_t> mMemoryPendingSubmit = 0;
    raw_ptr<DeviceBase> mDevice;
};
}  // namespace dawn::native

#endif  // SRC_DAWN_NATIVE_DYNAMICUPLOADER_H_
