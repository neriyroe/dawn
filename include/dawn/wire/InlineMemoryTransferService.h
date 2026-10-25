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

#ifndef INCLUDE_DAWN_WIRE_INLINEMEMORYTRANSFERSERVICE_H_
#define INCLUDE_DAWN_WIRE_INLINEMEMORYTRANSFERSERVICE_H_

#include <memory>

#include "dawn/wire/WireClient.h"
#include "dawn/wire/WireServer.h"
#include "dawn/wire/dawn_wire_export.h"

namespace dawn::wire {

// Dawn's inline memory transfer services for a client and server in one process, sharing the
// platform's shared memory manager: mappable buffers' data then skips the commands and replies.
struct InlineMemoryTransferServices {
    std::unique_ptr<client::MemoryTransferService> client;
    std::unique_ptr<server::MemoryTransferService> server;
};

DAWN_WIRE_EXPORT InlineMemoryTransferServices CreateInlineMemoryTransferServices();

}  // namespace dawn::wire

#endif  // INCLUDE_DAWN_WIRE_INLINEMEMORYTRANSFERSERVICE_H_
