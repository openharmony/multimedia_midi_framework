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

#ifndef MIDI_SERVICE_CONTROLLER_H
#define MIDI_SERVICE_CONTROLLER_H

#include <map>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <list>
#include "iremote_object.h"
#include "midi_info.h"
#include "midi_device_connection.h"
#include "midi_in_server.h"
#include "midi_device_mananger.h"
#include "imidi_device_open_callback.h"
#include <thread>
#include <condition_variable>

namespace OHOS {
namespace MIDI {

class DeviceClientContext {
public:
    DeviceClientContext(int64_t id, std::unordered_set<int32_t> clientIds) : deviceId(id), clients(std::move(clientIds))
    {}
    ~DeviceClientContext();
    int64_t deviceId;
    std::unordered_set<int32_t> clients;
    std::unordered_map<int64_t, std::shared_ptr<DeviceConnectionForInput>> inputDeviceconnections_;
};
struct PendingBleConnection {
    uint32_t clientId;
    sptr<IMidiDeviceOpenCallback> callback;
};

class MidiServiceController {
public:
    MidiServiceController();
    ~MidiServiceController();
    static MidiServiceController *GetInstance();
    void Init();
    int32_t CreateMidiInServer(const sptr<IRemoteObject> &object, sptr<IRemoteObject> &client, uint32_t &clientId);
    std::vector<std::map<int32_t, std::string>> GetDevices();
    std::vector<std::map<int32_t, std::string>> GetDevicePorts(int64_t deviceId);
    int32_t OpenDevice(uint32_t clientId, int64_t deviceId);
    int32_t OpenBleDevice(uint32_t clientId, const std::string &address, const sptr<IRemoteObject> &callbackObj);
    int32_t CloseDevice(uint32_t clientId, int64_t deviceId);
    int32_t OpenInputPort(
        uint32_t clientId, std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex);
    int32_t CloseInputPort(uint32_t clientId, int64_t deviceId, uint32_t portIndex);
    int32_t DestroyMidiClient(uint32_t clientId);
    void NotifyDeviceChange(DeviceChangeType change, DeviceInformation device);
    void NotifyError(int32_t code);

private:
    void ClosePortforDevice(
        uint32_t clientId, int64_t deviceId, std::shared_ptr<DeviceClientContext> deviceClientContext);
    int32_t CloseInputPortInner(uint32_t clientId, int64_t deviceId, uint32_t portIndex);
    void HandleBleOpenComplete(const std::string &address, bool success, int64_t deviceId, 
                                const std::map<int32_t, std::string> &deviceInfo);

    void ScheduleUnloadTask();
    void CancelUnloadTask();
    std::unordered_map<int64_t, std::shared_ptr<DeviceClientContext>> deviceClientContexts_;
    std::unordered_map<int32_t, sptr<MidiInServer>> clients_;
    
    // Map Address -> DeviceId (For quickly checking if a BLE address is already active)
    std::unordered_map<std::string, int64_t> activeBleDevices_;

    // Map Address -> List of waiting clients
    std::unordered_map<std::string, std::list<PendingBleConnection>> pendingBleConnections_;
 	 
    MidiDeviceManager deviceManager_{};
    static std::atomic<uint32_t> currentClientId_;
    std::mutex lock_;
    const int64_t UNLOAD_DELAY_TIME = 5 * 60 * 1000; // 5 minutes

    std::atomic<bool> isUnloadPending_{false};
    std::mutex unloadMutex_;
    std::condition_variable unloadCv_;
    std::thread unloadThread_;
};
}  // namespace MIDI
}  // namespace OHOS

#endif