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
#ifndef MIDI_CLIENT_H
#define MIDI_CLIENT_H
#include "native_midi_base.h"
#include "midi_info.h"
namespace OHOS {
namespace MIDI {

class MidiDevice {
public:
    virtual ~MidiDevice() = default;
    virtual OH_MidiStatusCode CloseDevice();
    virtual OH_MidiStatusCode OpenInputPort(uint32_t portIndex, OH_OnMidiReceived callback,
                                            void *userData);
    virtual OH_MidiStatusCode ClosePort(uint32_t portIndex);
};

class MidiClient {
public:
    virtual ~MidiClient() = default;
    static OH_MidiStatusCode CreateMidiClient(MidiClient **client, OH_MidiCallbacks callbacks, void *userData);
    virtual OH_MidiStatusCode Init(OH_MidiCallbacks callbacks, void *userData);
    virtual OH_MidiStatusCode GetDevices(OH_MidiDeviceInformation *infos, size_t *numDevices);
    virtual OH_MidiStatusCode OpenDevice(int64_t deviceId, MidiDevice **midiDevice);
    virtual OH_MidiStatusCode GetDevicePorts(int64_t deviceId, OH_MidiPortInformation *infos, size_t *numPorts);
    virtual OH_MidiStatusCode DestroyMidiClient();
};
} // namespace MIDI
} // namespace OHOS
#endif
