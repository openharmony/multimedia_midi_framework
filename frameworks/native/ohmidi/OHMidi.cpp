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
#define LOG_TAG "OHMidiClient"
#endif

#include "native_midi_base.h"
#include "native_midi.h"
#include "midi_client.h"
#include "midi_log.h"

OH_MIDIStatusCode OH_MIDIClient_Create(OH_MIDIClient **client, OH_MIDICallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(client != nullptr, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "client is nullptr");
    OHOS::MIDI::MidiClient *midiclient = nullptr;
    OH_MIDIStatusCode ret = OHOS::MIDI::MidiClient::CreateMidiClient(&midiclient, callbacks, userData);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "CreateMidiClient failed");
    *client = (OH_MIDIClient *)midiclient;
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_Destroy(OH_MIDIClient *client)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "convert builder failed");
    OH_MIDIStatusCode ret = midiclient->DestroyMidiClient();
    delete midiclient;
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "DestroyMidiClient failed");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_GetDeviceCount(const OH_MIDIClient *client, size_t *count)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    CHECK_AND_RETURN_RET_LOG(count != nullptr, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");

    midiclient->GetDevices(nullptr, count);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_GetDeviceInfos(const OH_MIDIClient *client,
                                               OH_MIDIDeviceInformation *infos,
                                               size_t capacity,
                                               size_t *actualDeviceCount)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    CHECK_AND_RETURN_RET_LOG(actualDeviceCount != nullptr && infos != nullptr,
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");

    size_t numDevices = capacity;
    OH_MIDIStatusCode ret = midiclient->GetDevices(infos, &numDevices);
    *actualDeviceCount = numDevices;  // Always set actual count
    return ret;
}

OH_MIDIStatusCode OH_MIDIClient_OpenDevice(OH_MIDIClient *client, int64_t deviceId, OH_MIDIDevice **device)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "Invalid client");

    CHECK_AND_RETURN_RET_LOG(device != nullptr, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    OHOS::MIDI::MidiDevice *midiDevice = nullptr;

    OH_MIDIStatusCode ret = midiclient->OpenDevice(deviceId, &midiDevice);

    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "OpenDevice failed");
    *device = (OH_MIDIDevice *)midiDevice;
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_OpenBLEDevice(OH_MIDIClient *client, const char *deviceAddr,
    OH_MIDIClient_OnDeviceOpened callback, void *userData)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient*) client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    CHECK_AND_RETURN_RET_LOG(deviceAddr != nullptr && callback != nullptr,
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    std::string deviceAddress(deviceAddr);
    OH_MIDIStatusCode ret = midiclient->OpenBleDevice(deviceAddress, callback, userData);

    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "OpenDevice failed");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_CloseDevice(OH_MIDIClient *client, OH_MIDIDevice *device)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient*) client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid deivce");
    OH_MIDIStatusCode ret = midiDevice->CloseDevice();
    delete midiDevice;
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "CloseDevice failed");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_GetPortCount(const OH_MIDIClient *client, int64_t deviceId, size_t *count)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    CHECK_AND_RETURN_RET_LOG(count != nullptr, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");

    return midiclient->GetDevicePorts(deviceId, nullptr, count);
}

OH_MIDIStatusCode OH_MIDIClient_GetPortInfos(const OH_MIDIClient *client,
                                             int64_t deviceId,
                                             OH_MIDIPortInformation *infos,
                                             size_t capacity,
                                             size_t *actualPortCount)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, OH_MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    CHECK_AND_RETURN_RET_LOG(actualPortCount != nullptr && infos != nullptr,
        OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");

    size_t numPorts = capacity;
    OH_MIDIStatusCode ret = midiclient->GetDevicePorts(deviceId, infos, &numPorts);
    *actualPortCount = numPorts;  // Always set actual count
    return ret;
}

OH_MIDIStatusCode OH_MIDIDevice_OpenInputPort(
    OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor, OH_MIDIDevice_OnReceived callback, void *userData)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid device");
    CHECK_AND_RETURN_RET_LOG(callback != nullptr && userData != nullptr, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT,
        "Invalid parameter");

    OH_MIDIStatusCode ret = midiDevice->OpenInputPort(descriptor, callback, userData);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "OpenInputPort falid");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_OpenOutputPort(OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid device");

    OH_MIDIStatusCode ret = midiDevice->OpenOutputPort(descriptor);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "OpenOutputPort falid");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_CloseInputPort(OH_MIDIDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid device");

    OH_MIDIStatusCode ret = midiDevice->CloseInputPort(portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "CloseInputPort failed");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_CloseOutputPort(OH_MIDIDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid device");

    OH_MIDIStatusCode ret = midiDevice->CloseOutputPort(portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "CloseOutputPort failed");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_Send(
    OH_MIDIDevice *device, uint32_t portIndex, const OH_MIDIEvent *events, uint32_t eventCount, uint32_t *eventsWritten)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid device");
    OH_MIDIStatusCode ret = midiDevice->Send(portIndex, events, eventCount, eventsWritten);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "send falid");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_SendSysEx(OH_MIDIDevice *device, uint32_t portIndex,
    const uint8_t *data, uint32_t byteSize)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid device");
    CHECK_AND_RETURN_RET_LOG(data != nullptr, OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    OH_MIDIStatusCode ret = midiDevice->SendSysEx(portIndex, data, byteSize);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "send falid");
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_FlushOutputPort(OH_MIDIDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, OH_MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid device");
    OH_MIDIStatusCode ret = midiDevice->FlushOutputPort(portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == OH_MIDI_STATUS_OK, ret, "FlushOutputPort failed");
    return OH_MIDI_STATUS_OK;
}
