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

#ifndef MIDI_SERVICE_CONTROLLER_H
#define MIDI_SERVICE_CONTROLLER_H

#include <map>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <memory>
#include "iremote_object.h"
#include "midi_info.h"
#include "midi_device_connection.h"
#include "midi_client_in_server.h"
#include "midi_device_mananger.h"

namespace OHOS {
namespace MIDI {

struct DeviceClientContext {
    int64_t deviceId;
    std::unordered_set<int32_t> clients;
    std::unordered_map<int64_t, std::shared_ptr<DeviceConnectionForInput>> inputDeviceconnections_;
    DeviceClientContext(int64_t id, std::unordered_set<int32_t> clientIds)
        : deviceId(id), clients(std::move(clientIds)) {}
}; //todo 改成class 方便析构时 关闭fd

class MidiServiceController {
public:
    MidiServiceController();
    ~MidiServiceController();
    static MidiServiceController* GetInstance();
    void Init();
    int32_t CreateClientInServer(std::shared_ptr<MidiServiceCallback> callback, sptr<IRemoteObject>& client, uint32_t &clientId);
    std::vector<std::map<int32_t, std::string>> GetDevices();
    std::vector<std::map<int32_t, std::string>> GetDevicePorts(int64_t deviceId);
    int32_t OpenDevice(uint32_t clientId, int64_t deviceId);
    int32_t CloseDevice(uint32_t clientId, int64_t deviceId);
    int32_t OpenInputPort(uint32_t clientId,
                            std::shared_ptr<SharedMidiRing> &buffer,
                            int64_t deviceId, uint32_t portIndex);
    int32_t CloseInputPort(uint32_t clientId, int64_t deviceId, uint32_t portIndex);
    int32_t DestroyMidiClient(uint32_t clientId);
    void NotifyDeviceChange(DeviceChangeType change, DeviceInformation devices);
    void NotifyError(int32_t code);
private:
    std::unordered_map<int64_t, DeviceClientContext> deviceClientContexts_;
    std::unordered_map<int32_t, std::shared_ptr<MidiClientInServer>> clients_;
    MidiDeviceManager deviceManager_{};
    static std::atomic<uint32_t> currentClientId_;
    std::mutex lock_;
};
} // namespace MIDI
} // namespace OHOS

#endif