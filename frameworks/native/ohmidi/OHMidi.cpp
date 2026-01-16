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
#define LOG_TAG "OHMidiClient"
#endif

#include "native_midi_base.h"
#include "native_midi.h"
#include "midi_client.h"
#include "midi_log.h"

OH_MIDIStatusCode OH_MIDIClientCreate(OH_MIDIClient **client, OH_MIDICallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(client != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "client is nullptr");
    OHOS::MIDI::MidiClient *midiclient = nullptr;
    OH_MIDIStatusCode ret = OHOS::MIDI::MidiClient::CreateMidiClient(&midiclient, callbacks, userData);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "CreateMidiClient falid");
    *client = (OH_MIDIClient *)midiclient;
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClientDestroy(OH_MIDIClient *client)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, MIDI_STATUS_INVALID_CLIENT, "convert builder failed");
    OH_MIDIStatusCode ret = midiclient->DestroyMidiClient();
    delete midiclient;
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "DestroyMidiClient falid");
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIGetDevices(OH_MIDIClient *client, OH_MIDIDeviceInformation *infos, size_t *numDevices)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    CHECK_AND_RETURN_RET_LOG(numDevices != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");

    return midiclient->GetDevices(infos, numDevices);
}

OH_MIDIStatusCode OH_MIDIOpenDevice(OH_MIDIClient *client, int64_t deviceId, OH_MIDIDevice **device)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, MIDI_STATUS_INVALID_CLIENT, "Invalid client");

    CHECK_AND_RETURN_RET_LOG(device != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    OHOS::MIDI::MidiDevice *midiDevice = nullptr;

    OH_MIDIStatusCode ret = midiclient->OpenDevice(deviceId, &midiDevice);

    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "OpenDevice falid");
    *device = (OH_MIDIDevice *)midiDevice;
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIOpenBleDevice(
    OH_MIDIClient *client, const char *deviceAddr, OH_MIDIDevice **device, int64_t *deviceId)
{
    (void)deviceAddr;
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDICloseDevice(OH_MIDIDevice *device)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid parameter");
    OH_MIDIStatusCode ret = midiDevice->CloseDevice();
    delete midiDevice;
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "CloseDevice falid");
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIGetDevicePorts(
    OH_MIDIClient *client, int64_t deviceId, OH_MIDIPortInformation *infos, size_t *numPorts)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient *)client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr, MIDI_STATUS_INVALID_CLIENT, "Invalid client");
    CHECK_AND_RETURN_RET_LOG(numPorts != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    return midiclient->GetDevicePorts(deviceId, infos, numPorts);
}

OH_MIDIStatusCode OH_MIDIOpenInputPort(
    OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor, OH_OnMIDIReceived callback, void *userData)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, MIDI_STATUS_INVALID_DEVICE_HANDLE, "Invalid parameter");

    OH_MIDIStatusCode ret = midiDevice->OpenInputPort(descriptor.portIndex, callback, userData);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "OpenInputPort falid");
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIOpenOutputPort(OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor)
{
    (void)descriptor;
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIClosePort(OH_MIDIDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice *)device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr, MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");

    OH_MIDIStatusCode ret = midiDevice->ClosePort(portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "OpenInputPort falid");
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDISend(
    OH_MIDIDevice *device, uint32_t portIndex, OH_MIDIEvent *events, uint32_t eventCount, uint32_t *eventsWritten)
{
    (void)portIndex;
    (void)events;
    (void)eventCount;
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDISendSysEx(OH_MIDIDevice *device, uint32_t portIndex, uint8_t *data, uint32_t byteSize)
{
    (void)portIndex;
    (void)data;
    (void)byteSize;
    return MIDI_STATUS_OK;
}

OH_MIDIStatusCode OH_MIDIFlushOutputPort(OH_MIDIDevice *device, uint32_t portIndex)
{
    (void)portIndex;
    return MIDI_STATUS_OK;
}
