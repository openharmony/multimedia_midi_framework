/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
#define LOG_TAG "MidiServer"
#endif

#include "midi_server.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "midi_log.h"
#include "midi_listener_callback.h"
namespace OHOS {
namespace MIDI {
REGISTER_SYSTEM_ABILITY_BY_ID(MidiServer, MIDI_SERVICE_ID, false)

MidiServer::MidiServer(int32_t systemAbilityId, bool runOnCreate) : SystemAbility(systemAbilityId, runOnCreate) {}

void MidiServer::OnStart()
{
    controller_ = MidiServiceController::GetInstance();
    controller_->Init();
    Publish(this);
}

void MidiServer::OnDump() {}

int32_t MidiServer::CreateClientInServer(const sptr<IRemoteObject> &object, sptr<IRemoteObject> &client,
                                         uint32_t &clientId)
{
    CHECK_AND_RETURN_RET_LOG(controller_, MIDI_STATUS_UNKNOWN_ERROR, "controller_ is nullptr");
    sptr<IMidiCallback> listener = iface_cast<IMidiCallback>(object);
    CHECK_AND_RETURN_RET_LOG(listener, MIDI_STATUS_UNKNOWN_ERROR, "listener is nullptr");
    std::shared_ptr<MidiListenerCallback> callback = std::make_shared<MidiListenerCallback>(listener);
    CHECK_AND_RETURN_RET_LOG(callback, MIDI_STATUS_UNKNOWN_ERROR, "callback is nullptr");
    return controller_->CreateClientInServer(callback, client, clientId);
}

} // namespace MIDI
} // namespace OHOS