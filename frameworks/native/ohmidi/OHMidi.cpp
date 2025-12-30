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

OH_MidiStatusCode OH_MidiClient_Create(OH_MidiClient **client, OH_MidiCallbacks callbacks, void *userData)
{
    CHECK_AND_RETURN_RET_LOG(client != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT,"client is nullptr");
    OHOS::MIDI::MidiClient *midiclient = nullptr;
    OH_MidiStatusCode ret = OHOS::MIDI::MidiClient::CreateMidiClient(&midiclient, callbacks, userData);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "CreateMidiClient falid");
    *client = (OH_MidiClient*)midiclient;
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiClient_Destroy(OH_MidiClient *client)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient*) client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT,"convert builder failed");
    OH_MidiStatusCode ret = midiclient->DestroyMidiClient();
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "DestroyMidiClient falid");
    delete midiclient;
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiGetDevices(OH_MidiClient *client, OH_MidiDeviceInformation *infos, size_t *numDevices)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient*) client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr && numDevices != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT,"Invalid parameter");

    return midiclient->GetDevices(infos, numDevices);
}

OH_MidiStatusCode OH_MidiOpenDevice(OH_MidiClient *client, int64_t deviceId, OH_MidiDevice **device)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient*) client;

    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr && device != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT,"Invalid parameter");
    OHOS::MIDI::MidiDevice *midiDevice = nullptr;

    OH_MidiStatusCode ret = midiclient->OpenDevice(deviceId, &midiDevice);
    
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "OpenDevice falid");
    *device = (OH_MidiDevice*) midiDevice;
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiOpenBleDevice(OH_MidiClient *client, const char *deviceAddr, OH_MidiDevice **device)
{
    (void)deviceAddr;
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiCloseDevice(OH_MidiDevice *device)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice*) device;

    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    OH_MidiStatusCode ret = midiDevice->CloseDevice();
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "CloseDevice falid");
    delete midiDevice;
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiGetDevicePorts(OH_MidiClient *client, int64_t deviceId, OH_MidiPortInformation *infos, size_t *numPorts)
{
    OHOS::MIDI::MidiClient *midiclient = (OHOS::MIDI::MidiClient*) client;
    CHECK_AND_RETURN_RET_LOG(midiclient != nullptr && numPorts != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    return midiclient->GetDevicePorts(deviceId, infos, numPorts);
}

OH_MidiStatusCode OH_MidiOpenInputPort(OH_MidiDevice *device,
                                       uint32_t portIndex,
                                       OH_OnMidiReceived callback,
                                       void *userData)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice*) device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    
    OH_MidiStatusCode ret = midiDevice->OpenInputPort(portIndex, callback, userData);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "OpenInputPort falid");
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiOpenOutputPort(OH_MidiDevice *device,
                                        OH_MidiPortDescriptor descriptor)
{
    (void)descriptor;
    
    
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiClosePort(OH_MidiDevice *device, uint32_t portIndex)
{
    OHOS::MIDI::MidiDevice *midiDevice = (OHOS::MIDI::MidiDevice*) device;
    CHECK_AND_RETURN_RET_LOG(midiDevice != nullptr,
        MIDI_STATUS_GENERIC_INVALID_ARGUMENT, "Invalid parameter");
    
    OH_MidiStatusCode ret = midiDevice->ClosePort(portIndex);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, ret, "OpenInputPort falid");
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiSend(OH_MidiDevice *device,
                              uint32_t portIndex,
                              OH_MidiEvent *events,
                              uint32_t eventCount,
                              uint32_t *eventsWritten)
{
    (void)portIndex;
    (void)events;
    (void)eventCount;

    
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiSendSysEx(OH_MidiDevice *device,
                                   uint32_t portIndex,
                                   uint8_t *data,
                                   uint32_t byteSize)
{
    (void)portIndex;
    (void)data;
    (void)byteSize;
    
    
    return MIDI_STATUS_OK;
}

OH_MidiStatusCode OH_MidiFlushOutputPort(OH_MidiDevice *device, uint32_t portIndex)
{
    (void)portIndex;
    
    return MIDI_STATUS_OK;
}
