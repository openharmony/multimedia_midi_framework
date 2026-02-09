/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LOG_TAG
#define LOG_TAG "ClientConnectionInServer"
#endif

#include <memory>

#include "native_midi_base.h"
#include "midi_log.h"
#include "midi_client_connection.h"
#include "securec.h"

namespace OHOS {
namespace MIDI {

std::shared_ptr<MidiSharedRing> ClientConnectionInServer::GetRingBuffer()
{
    return sharedRingBuffer_;
}

int32_t ClientConnectionInServer::CreateRingBuffer(int fd)
{
    auto fdObject = std::make_shared<UniqueFd>(fd);
    sharedRingBuffer_ = MidiSharedRing::CreateFromLocal(DEFAULT_RING_BUFFER_SIZE, fdObject);
    CHECK_AND_RETURN_RET_LOG(sharedRingBuffer_ != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "create fail");

    memset_s(sharedRingBuffer_->GetDataBase(), sharedRingBuffer_->GetCapacity(), 0,
             sharedRingBuffer_->GetCapacity());
    return MIDI_STATUS_OK;
}


int32_t ClientConnectionInServer::TrySendToClient(const MidiEventInner& event)
{
    CHECK_AND_RETURN_RET(sharedRingBuffer_->TryWriteEvent(event) == MidiStatusCode::OK,
        MIDI_STATUS_UNKNOWN_ERROR, "try send event fail");
    return MIDI_STATUS_OK;
}

bool ClientConnectionInServer::EnqueueNonRealtime(std::vector<uint32_t>&& payloadWords,
                                                  std::chrono::steady_clock::time_point dueTime,
                                                  uint64_t timestamp)
{
    if (IsPendingFull()) {
        return false;
    }
    PendingEvent event;
    event.due = dueTime;
    event.timestamp = timestamp;
    event.data = std::move(payloadWords);
    pending_.push(std::move(event));
    return true;
}

const ClientConnectionInServer::PendingEvent* ClientConnectionInServer::PeekPendingTop() const
{
    if (pending_.empty()) return nullptr;
    return &pending_.top();
}

bool ClientConnectionInServer::PopPendingTop(PendingEvent& out)
{
    if (pending_.empty()) return false;
    out = pending_.top();
    pending_.pop();
    return true;
}

void ClientConnectionInServer::Flush()
{
    std::priority_queue<PendingEvent, std::vector<PendingEvent>, PendingGreater> emptyPending;
    pending_.swap(emptyPending);
    sharedRingBuffer_->Flush();
}
} // namespace MIDI
} // namespace OHOS