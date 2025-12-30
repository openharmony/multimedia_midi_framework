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
#define LOG_TAG "MidiClientInServer"
#endif

#include "midi_log.h"
#include "midi_service_controller.h"
namespace OHOS {
namespace MIDI {
MidiClientInServer::MidiClientInServer(uint32_t id, std::shared_ptr<MidiServiceCallback> callback)
{
    clientId_ = id;
    callback_ = callback;
}

int32_t MidiClientInServer::GetDevices(std::vector<std::map<int32_t, std::string>> &devices) //todo 增加传结构体，为其实现序列化和返序列化
{
    devices = MidiServiceController::GetInstance()->GetDevices();
    return MIDI_STATUS_OK;
}

int32_t MidiClientInServer::GetDevicePorts(int64_t deviceId, std::vector<std::map<int32_t, std::string>> &ports) 
{
    ports = MidiServiceController::GetInstance()->GetDevicePorts(deviceId);
    return MIDI_STATUS_OK;
}

int32_t MidiClientInServer::OpenDevice(int64_t deviceId)
{
    MIDI_INFO_LOG("OpenDevice");

    return MidiServiceController::GetInstance()->OpenDevice(clientId_, deviceId);
}


int32_t MidiClientInServer::OpenInputPort(std::shared_ptr<SharedMidiRing> &buffer,
        int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG("deviceId[%{public}" PRId64 "]---->portIndex[%{public}u]",
        deviceId, portIndex);
    return MidiServiceController::GetInstance()->OpenInputPort(clientId_, buffer,
        deviceId, portIndex);
}

int32_t MidiClientInServer::CloseInputPort(int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG("deviceId[%{public}" PRId64 "]--xx-->portIndex[%{public}u]",
        deviceId, portIndex);
    return MidiServiceController::GetInstance()->CloseInputPort(clientId_, deviceId, portIndex);
}


int32_t MidiClientInServer::CloseDevice(int64_t deviceId)
{
    return MidiServiceController::GetInstance()->CloseDevice(clientId_, deviceId);
}

int32_t MidiClientInServer::DestroyMidiClient()
{
    return MidiServiceController::GetInstance()->DestroyMidiClient(clientId_);
}

void MidiClientInServer::NotifyDeviceChange(DeviceChangeType change, std::map<int32_t, std::string> deviceInfo)
{
    CHECK_AND_RETURN(callback_ != nullptr);
    callback_->NotifyDeviceChange(change, deviceInfo);
}

void MidiClientInServer::NotifyError(int32_t code)
{
    CHECK_AND_RETURN(callback_ != nullptr);
    callback_->NotifyError(code);
}

} // namespace MIDI
} // namespace OHOS

