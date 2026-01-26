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

#ifndef MIDI_SERVICE_DEATH_RECIPIENT_H
#define MIDI_SERVICE_DEATH_RECIPIENT_H

#include "iremote_object.h"
#include "nocopyable.h"

namespace OHOS {
namespace MIDI {
class MidiServiceDeathRecipient : public IRemoteObject::DeathRecipient {
public:
    explicit MidiServiceDeathRecipient(uint32_t clientId) : clientId_(clientId) {}
    virtual ~MidiServiceDeathRecipient() = default;
    DISALLOW_COPY_AND_MOVE(MidiServiceDeathRecipient);
    void OnRemoteDied(const wptr<IRemoteObject> &remote)
    {
        (void)remote;
        if (diedCb_ != nullptr) {
            diedCb_(clientId_);
        }
    }
    using NotifyCbFunc = std::function<void(uint32_t)>;
    void SetNotifyCb(NotifyCbFunc func) { diedCb_ = func; }

private:
    NotifyCbFunc diedCb_ = nullptr;
    uint32_t clientId_ = 0;
};
} // namespace MIDI
} // namespace OHOS
#endif // AUDIO_SERVER_DEATH_RECIPIENT_H
