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

#ifndef MIDI_CLIENT_IN_SERVER_H
#define MIDI_CLIENT_IN_SERVER_H
#include "midi_info.h"
#include "midi_shared_ring.h"
#include "ipc_midi_client_in_server_stub.h"
namespace OHOS {
namespace MIDI {
class MidiClientInServer : public IpcMidiClientInServerStub {
public:
    MidiClientInServer(uint32_t id, std::shared_ptr<MidiServiceCallback> callback);
    virtual ~MidiClientInServer();
    int32_t GetDevices(std::vector<std::map<int32_t, std::string>> &devices) override;
    int32_t GetDevicePorts(int64_t deviceId, std::vector<std::map<int32_t, std::string>> &ports) override;
    int32_t OpenDevice(int64_t deviceId) override;
    int32_t CloseDevice(int64_t deviceId) override;
    int32_t OpenInputPort(std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex) override;
    int32_t OpenOutputPort(std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex) override;
    int32_t CloseInputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t DestroyMidiClient() override;
    void NotifyDeviceChange(DeviceChangeType change, std::map<int32_t, std::string> deviceInfo);
    void NotifyError(int32_t code);

private:
    uint32_t clientId_;
    std::shared_ptr<MidiServiceCallback> callback_;
};
} // namespace MIDI
} // namespace OHOS
#endif
