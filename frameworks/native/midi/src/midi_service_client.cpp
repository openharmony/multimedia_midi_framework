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
#define LOG_TAG "MidiServiceClient"
#endif

#include <algorithm>

#include "midi_service_client.h"
#include "midi_log.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "midi_service_death_recipent.h"

namespace OHOS {
namespace MIDI {
OH_MidiStatusCode MidiServiceClient::Init(sptr<MidiCallbackStub> callback, uint32_t &clientId)
{
    
    std::lock_guard lock(lock_);
    auto samgr = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    CHECK_AND_RETURN_RET_LOG(samgr != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE, "Get samgr failed.");

    sptr<IRemoteObject> object = samgr->LoadSystemAbility(MIDI_SERVICE_ID, 4);
    CHECK_AND_RETURN_RET_LOG(object != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "midi service remote object is NULL.");

    sptr<IMidiService> gsp = iface_cast<IMidiService>(object);
    CHECK_AND_RETURN_RET_LOG(gsp != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "init gsp is NULL.");
    sptr<IRemoteObject> ipcProxy = nullptr;
    
    auto ret = gsp->CreateClientInServer(callback, ipcProxy, clientId);
    CHECK_AND_RETURN_RET(ret == MIDI_STATUS_OK, (OH_MidiStatusCode)ret);
    ipc_ = iface_cast<IIpcMidiClientInServer>(ipcProxy);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");
    callback_ = callback;
    sptr<MidiServiceDeathRecipient> deathRecipient_ = new(std::nothrow) MidiServiceDeathRecipient(0);
    deathRecipient_->SetNotifyCb(
        [this](uint32_t clientId) {
            CHECK_AND_RETURN(this->callback_ != nullptr);
            this->callback_->NotifyError(MIDI_STATUS_SERVICE_DIED);
        }
    );
    object->AddDeathRecipient(deathRecipient_);
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode MidiServiceClient::GetDevices(std::vector<std::map<int32_t, std::string>> &deviceInfos)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");
    
    return (OH_MidiStatusCode)ipc_->GetDevices(deviceInfos);
}

OH_MidiStatusCode MidiServiceClient::OpenDevice(int64_t deviceId)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");

    return (OH_MidiStatusCode)ipc_->OpenDevice(deviceId);
}

OH_MidiStatusCode MidiServiceClient::CloseDevice(int64_t deviceId)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");

    return (OH_MidiStatusCode)ipc_->CloseDevice(deviceId);
}

OH_MidiStatusCode MidiServiceClient::GetDevicePorts(int64_t deviceId, std::vector<std::map<int32_t, std::string>> &portInfos)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");

    return (OH_MidiStatusCode)ipc_->GetDevicePorts(deviceId, portInfos);
}

OH_MidiStatusCode MidiServiceClient::OpenInputPort(std::shared_ptr<SharedMidiRing> &buffer,
                                                    int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");
    return (OH_MidiStatusCode)ipc_->OpenInputPort(buffer, deviceId, portIndex);
}

OH_MidiStatusCode MidiServiceClient::CloseInputPort(int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");
    return (OH_MidiStatusCode)ipc_->CloseInputPort(deviceId, portIndex);
}

OH_MidiStatusCode MidiServiceClient::DestroyMidiClient()
{
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, MIDI_STATUS_GENERIC_IPC_FAILURE,
        "ipc_ is NULL.");
    auto ret = ipc_->DestroyMidiClient();
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, (OH_MidiStatusCode)ret, "DestroyMidiClient failed");
    ipc_ = nullptr;
    return MIDI_STATUS_OK;
}
} // namespace MIDI
} // namespace OHOS