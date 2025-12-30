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
#ifndef MIDI_CLIENT_PRIVATE_H
#define MIDI_CLIENT_PRIVATE_H
#include "midi_client.h"
#include "midi_service_client.h"
namespace OHOS {
namespace MIDI {

class MidiClientCallback;
class MidiDevicePrivate : public MidiDevice {
public:
    MidiDevicePrivate(std::shared_ptr<MidiServiceClient> midiServiceClient, int64_t deviceId);
    virtual ~MidiDevicePrivate();
    OH_MidiStatusCode CloseDevice() override;
private:
    std::shared_ptr<MidiServiceClient> ipc_;
    int64_t deviceId_;
};

class MidiClientPrivate : public MidiClient {
public:
    MidiClientPrivate();
    virtual ~MidiClientPrivate();
    OH_MidiStatusCode Init(OH_MidiCallbacks callbacks, void *userData) override;
    OH_MidiStatusCode GetDevices(OH_MidiDeviceInformation *infos, size_t *numDevices) override;
    OH_MidiStatusCode OpenDevice(int64_t deviceId, MidiDevice **midiDevice) override;
    OH_MidiStatusCode GetDevicePorts(int64_t deviceId, OH_MidiPortInformation *infos, size_t *numPorts) override;
    OH_MidiStatusCode DestroyMidiClient() override;
private:
    std::shared_ptr<MidiServiceClient> ipc_;
    uint32_t clientId_;
    std::vector<OH_MidiDeviceInformation> deviceInfos_;
    sptr<MidiClientCallback> callback_;
    std::mutex mutex_;
};
} // namespace MIDI
} // namespace OHOS
#endif
