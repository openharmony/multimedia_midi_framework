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
#ifndef MIDI_CLIENT_H
#define MIDI_CLIENT_H
#include "native_midi_base.h"
#include "midi_info.h"
namespace OHOS {
namespace MIDI {

class MidiDevice {
public:
    virtual ~MidiDevice() = default;
    virtual OH_MIDIStatusCode CloseDevice();
    virtual OH_MIDIStatusCode OpenInputPort(OH_MIDIPortDescriptor descriptor,
                                                OH_MIDIDevice_OnReceived callback, void *userData);
    virtual OH_MIDIStatusCode OpenOutputPort(OH_MIDIPortDescriptor descriptor);
    virtual OH_MIDIStatusCode ClosePort(uint32_t portIndex);
    virtual OH_MIDIStatusCode Send(uint32_t portIndex, OH_MIDIEvent *events,
                                    uint32_t eventCount, uint32_t *eventsWritten);
    virtual OH_MIDIStatusCode SendSysEx(uint32_t portIndex, uint8_t *data, uint32_t byteSize);
    virtual OH_MIDIStatusCode FlushOutputPort(uint32_t portIndex);
};

class MidiClient {
public:
    virtual ~MidiClient() = default;
    static OH_MIDIStatusCode CreateMidiClient(MidiClient **client, OH_MIDICallbacks callbacks, void *userData);
    virtual OH_MIDIStatusCode Init(OH_MIDICallbacks callbacks, void *userData);
    virtual OH_MIDIStatusCode GetDevices(OH_MIDIDeviceInformation *infos, size_t *numDevices);
    virtual OH_MIDIStatusCode OpenDevice(int64_t deviceId, MidiDevice **midiDevice);
    virtual OH_MIDIStatusCode OpenBleDevice(std::string address, OH_MIDIClient_OnDeviceOpened callback, void *userData);
    virtual OH_MIDIStatusCode GetDevicePorts(int64_t deviceId, OH_MIDIPortInformation *infos, size_t *numPorts);
    virtual OH_MIDIStatusCode DestroyMidiClient();
};
} // namespace MIDI
} // namespace OHOS
#endif
