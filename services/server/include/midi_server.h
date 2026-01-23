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

#ifndef MIDI_SERVER_H
#define MIDI_SERVER_H

#include "midi_info.h"
#include "ipc_skeleton.h"
#include "iremote_stub.h"
#include "system_ability.h"
#include "midi_service_stub.h"
#include "midi_service_controller.h"

namespace OHOS {
namespace MIDI {
class MidiServer : public SystemAbility, public MidiServiceStub {
    DECLARE_SYSTEM_ABILITY(MidiServer);

public:
    DISALLOW_COPY_AND_MOVE(MidiServer);
    explicit MidiServer(int32_t systemAbilityId, bool runOnCreate = true);
    virtual ~MidiServer() = default;
    void OnDump() override;
    void OnStart() override;
    int32_t CreateMidiInServer(const sptr<IRemoteObject> &object, sptr<IRemoteObject> &client,
                                 uint32_t &clientId) override;

private:
    MidiServiceController *controller_;
};
} // namespace MIDI
} // namespace OHOS
#endif