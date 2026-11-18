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
    if (!(client != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_Create】client is nullptr");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }
    OHOS::MIDI::MidiClient *midiclient = nullptr;
    OH_MIDIStatusCode ret = OHOS::MIDI::MidiClient::CreateMidiClient(&midiclient, callbacks, userData);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_Create】CreateMidiClient failed");
        return ret;
    }
    *client = (OH_MIDIClient *)midiclient;
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_Destroy(OH_MIDIClient *client)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_Destroy】convert builder failed");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }
    OH_MIDIStatusCode ret = midiclient->DestroyMidiClient();
    delete midiclient;
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_Destroy】DestroyMidiClient failed");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_GetDeviceCount(const OH_MIDIClient *client, size_t *count)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetDeviceCount】Invalid client");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }
    if (!(count != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetDeviceCount】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }

    midiclient->GetDevices(nullptr, count);
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_GetDeviceInfos(const OH_MIDIClient *client,
                                               OH_MIDIDeviceInformation *infos,
                                               size_t capacity,
                                               size_t *actualDeviceCount)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetDeviceInfos】Invalid client");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }
    if (!(actualDeviceCount != nullptr && infos != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetDeviceInfos】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }

    size_t numDevices = capacity;
    OH_MIDIStatusCode ret = midiclient->GetDevices(infos, &numDevices);
    *actualDeviceCount = numDevices;  // Always set actual count
    return ret;
}

OH_MIDIStatusCode OH_MIDIClient_OpenDevice(OH_MIDIClient *client, int64_t deviceId, OH_MIDIDevice **device)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_OpenDevice】Invalid client");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }

    if (!(device != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_OpenDevice】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }
    OHOS::MIDI::MidiDevice *midiDevice = nullptr;

    OH_MIDIStatusCode ret = midiclient->OpenDevice(deviceId, &midiDevice);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_OpenDevice】OpenDevice failed");
        return ret;
    }
    *device = (OH_MIDIDevice *)midiDevice;
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_OpenBLEDevice(OH_MIDIClient *client, const char *deviceAddr,
    OH_MIDIClient_OnDeviceOpened callback, void *userData)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient*) client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_OpenBLEDevice】Invalid client");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }
    if (!(deviceAddr != nullptr && callback != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_OpenBLEDevice】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }
    std::string deviceAddress(deviceAddr);
    OH_MIDIStatusCode ret = midiclient->OpenBleDevice(deviceAddress, callback, userData);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_OpenBLEDevice】OpenDevice failed");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_CloseDevice(OH_MIDIClient *client, OH_MIDIDevice *device)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_CloseDevice】Invalid client");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_CloseDevice】Invalid deivce");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }
    OH_MIDIStatusCode ret = midiclient->CloseAndRemoveDevice(midiDevice);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_CloseDevice】CloseDevice failed");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClient_GetPortCount(const OH_MIDIClient *client, int64_t deviceId, size_t *count)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetPortCount】Invalid client");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }
    if (!(count != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetPortCount】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }

    return midiclient->GetDevicePorts(deviceId, nullptr, count);
}

OH_MIDIStatusCode OH_MIDIClient_GetPortInfos(const OH_MIDIClient *client,
                                             int64_t deviceId,
                                             OH_MIDIPortInformation *infos,
                                             size_t capacity,
                                             size_t *actualPortCount)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    if (!(midiclient != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetPortInfos】Invalid client");
        return OH_MIDI_STATUS_INVALID_CLIENT;
    }
    if (!(actualPortCount != nullptr && infos != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIClient_GetPortInfos】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }

    size_t numPorts = capacity;
    OH_MIDIStatusCode ret = midiclient->GetDevicePorts(deviceId, infos, &numPorts);
    *actualPortCount = numPorts;  // Always set actual count
    return ret;
}

OH_MIDIStatusCode OH_MIDIDevice_OpenInputPort(
    OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor, OH_MIDIDevice_OnReceived callback, void *userData)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_OpenInputPort】Invalid device");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }
    if (!(callback != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_OpenInputPort】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }

    OH_MIDIStatusCode ret = midiDevice->OpenInputPort(descriptor, callback, userData);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_OpenInputPort】OpenInputPort falid");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_OpenOutputPort(OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_OpenOutputPort】Invalid device");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }

    OH_MIDIStatusCode ret = midiDevice->OpenOutputPort(descriptor);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_OpenOutputPort】OpenOutputPort falid");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_CloseInputPort(OH_MIDIDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_CloseInputPort】Invalid device");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }

    OH_MIDIStatusCode ret = midiDevice->CloseInputPort(portIndex);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_CloseInputPort】CloseInputPort failed");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_CloseOutputPort(OH_MIDIDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_CloseOutputPort】Invalid device");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }

    OH_MIDIStatusCode ret = midiDevice->CloseOutputPort(portIndex);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_CloseOutputPort】CloseOutputPort failed");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_Send(
    OH_MIDIDevice *device, uint32_t portIndex, const OH_MIDIEvent *events, uint32_t eventCount, uint32_t *eventsWritten)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_Send】Invalid device");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }
    OH_MIDIStatusCode ret = midiDevice->Send(portIndex, events, eventCount, eventsWritten);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_Send】send falid");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_SendSysEx(OH_MIDIDevice *device, uint32_t portIndex,
    const uint8_t *data, uint32_t byteSize)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_SendSysEx】Invalid device");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }
    if (!(data != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_SendSysEx】Invalid parameter");
        return OH_MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }
    OH_MIDIStatusCode ret = midiDevice->SendSysEx(portIndex, data, byteSize);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_SendSysEx】send falid");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIDevice_FlushOutputPort(OH_MIDIDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    if (!(midiDevice != nullptr)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_FlushOutputPort】Invalid device");
        return OH_MIDI_STATUS_INVALID_DEVICE_HANDLE;
    }
    OH_MIDIStatusCode ret = midiDevice->FlushOutputPort(portIndex);
    if (!(ret == OH_MIDI_STATUS_OK)) {
        HILOG_COMM_ERROR("【OH_MIDIDevice_FlushOutputPort】FlushOutputPort failed");
        return ret;
    }
    return OH_MIDI_STATUS_OK;
}
