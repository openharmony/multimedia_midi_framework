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
#define LOG_TAG "MidiServiceClient"
#endif

#include <algorithm>

#include "midi_service_client.h"
#include "midi_log.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "securec.h"

namespace OHOS {
namespace MIDI {

static OH_MIDIStatusCode GetMidiStatusCode(int32_t statusCode)
{
    auto ret = (OH_MIDIStatusCode)statusCode;
    return (ret >= OH_MIDI_STATUS_OK && ret < OH_MIDI_STATUS_SYSTEM_ERROR) ? ret :
        OH_MIDI_STATUS_GENERIC_IPC_FAILURE;
}

MidiServiceClient::~MidiServiceClient()
{
    MIDI_INFO_LOG("~MidiServiceClient");
}

OH_MIDIStatusCode MidiServiceClient::Init(sptr<MidiCallbackStub> callback, uint32_t &clientId)
{
    auto samgr = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    CHECK_AND_RETURN_RET_LOG(samgr != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "Get samgr failed.");

    sptr<IRemoteObject> object = samgr->LoadSystemAbility(MIDI_SERVICE_ID, 4); // 4s
    CHECK_AND_RETURN_RET_LOG(object != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE,
        "midi service remote object is NULL.");

    sptr<IMidiService> gsp = iface_cast<IMidiService>(object);
    CHECK_AND_RETURN_RET_LOG(gsp != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "init gsp is NULL.");
    sptr<IRemoteObject> ipcProxy = nullptr;

    auto ret = gsp->CreateMidiInServer(callback, ipcProxy, clientId);
    CHECK_AND_RETURN_RET(ret == OH_MIDI_STATUS_OK, (OH_MIDIStatusCode)ret);
    ipc_ = iface_cast<IIpcMidiInServer>(ipcProxy);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    callback_ = callback;
    deathRecipient_ = new(std::nothrow) MidiServiceDeathRecipient(0);
    deathRecipient_->SetNotifyCb(
        [this](uint32_t clientId) {
            CHECK_AND_RETURN(this->callback_ != nullptr);
            this->callback_->NotifyError(OH_MIDI_STATUS_SERVICE_DIED);
        }
    );
    object->AddDeathRecipient(deathRecipient_);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode MidiServiceClient::GetDevices(std::vector<std::map<int32_t, std::string>> &deviceInfos)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->GetDevices(deviceInfos);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::OpenDevice(int64_t deviceId)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->OpenDevice(deviceId);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::OpenBleDevice(std::string address, sptr<MidiDeviceOpenCallbackStub> callback)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->OpenBleDevice(address, callback);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::CloseDevice(int64_t deviceId)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->CloseDevice(deviceId);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::GetDevicePorts(int64_t deviceId,
                                                    std::vector<std::map<int32_t, std::string>> &portInfos)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->GetDevicePorts(deviceId, portInfos);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::OpenInputPort(std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId,
                                                   uint32_t portIndex)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->OpenInputPort(buffer, deviceId, portIndex);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::OpenOutputPort(std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId,
                                                    uint32_t portIndex)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->OpenOutputPort(buffer, deviceId, portIndex);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::FlushOutputPort(int64_t deviceId, uint32_t portIndex)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->FlushOutputPort(deviceId, portIndex);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::CloseInputPort(int64_t deviceId, uint32_t portIndex)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->CloseInputPort(deviceId, portIndex);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::CloseOutputPort(int64_t deviceId, uint32_t portIndex)
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto ret = ipc_->CloseOutputPort(deviceId, portIndex);
    return GetMidiStatusCode(ret);
}

OH_MIDIStatusCode MidiServiceClient::DestroyMidiClient()
{
    std::lock_guard lock(lock_);
    CHECK_AND_RETURN_RET_LOG(ipc_ != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "ipc_ is NULL.");
    auto samgr = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    CHECK_AND_RETURN_RET_LOG(samgr != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE, "Get samgr failed.");

    sptr<IRemoteObject> object = samgr->CheckSystemAbility(MIDI_SERVICE_ID);
    CHECK_AND_RETURN_RET_LOG(object != nullptr, OH_MIDI_STATUS_GENERIC_IPC_FAILURE,
        "midi service remote object is NULL.");
    object->RemoveDeathRecipient(deathRecipient_);
    deathRecipient_ = nullptr;
    auto ret = ipc_->DestroyMidiClient();
    ipc_ = nullptr;
    callback_ = nullptr;
    CHECK_AND_RETURN_RET_LOG(GetMidiStatusCode(ret) == OH_MIDI_STATUS_OK, GetMidiStatusCode(ret),
        "DestroyMidiClient failed");
    return OH_MIDI_STATUS_OK;
}
} // namespace MIDI
} // namespace OHOS