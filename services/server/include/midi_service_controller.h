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

// Enable test helper methods when building unit tests
#ifdef UNIT_TEST_SUPPORT
#define MIDI_TEST_VISIBLE
#else
#define MIDI_TEST_VISIBLE
#endif

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
    std::unordered_map<int64_t, std::shared_ptr<DeviceConnectionForOutput>> outputDeviceconnections_;
};
struct PendingBleConnection {
    uint32_t clientId;
    sptr<IMidiDeviceOpenCallback> callback;
};

class MidiServiceController : public std::enable_shared_from_this<MidiServiceController> {
public:
    MidiServiceController();
    ~MidiServiceController();
    static std::shared_ptr<MidiServiceController> GetInstance();
    void Init();
    int32_t CreateMidiInServer(const sptr<IRemoteObject> &object, sptr<IRemoteObject> &client, uint32_t &clientId);
    std::vector<std::map<int32_t, std::string>> GetDevices();
    std::vector<std::map<int32_t, std::string>> GetDevicePorts(int64_t deviceId);
    int32_t OpenDevice(uint32_t clientId, int64_t deviceId);
    int32_t OpenBleDevice(uint32_t clientId, const std::string &address, const sptr<IRemoteObject> &callbackObj);
    int32_t CloseDevice(uint32_t clientId, int64_t deviceId);
    int32_t OpenInputPort(
        uint32_t clientId, std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex);
    int32_t OpenOutputPort(
        uint32_t clientId, std::shared_ptr<MidiSharedRing> &buffer, int64_t deviceId, uint32_t portIndex);
    int32_t CloseInputPort(uint32_t clientId, int64_t deviceId, uint32_t portIndex);
    int32_t CloseOutputPort(uint32_t clientId, int64_t deviceId, uint32_t portIndex);
    int32_t DestroyMidiClient(uint32_t clientId);
    void NotifyDeviceChange(DeviceChangeType change, DeviceInformation device);
    void NotifyError(int32_t code);

    // Runtime configuration (callable from tests)
    void SetUnloadDelay(int64_t delayMs) { unloadDelayTime_ = delayMs; }

#ifdef UNIT_TEST_SUPPORT
    /**
     * @brief Test helper: Get the device manager instance for testing
     * @return Shared pointer to the device manager
     * @note Only available when UNIT_TEST_SUPPORT is defined
     */
    std::shared_ptr<MidiDeviceManager> GetDeviceManagerForTest() const { return deviceManager_; }

    /**
     * @brief Test helper: Clear all internal state for test isolation
     * @note Only available when UNIT_TEST_SUPPORT is defined
     */
    void ClearStateForTest();

    /**
     * @brief Test helper: Check if a device has client context
     * @param deviceId The device ID to check
     * @return true if device context exists, false otherwise
     * @note Only available when UNIT_TEST_SUPPORT is defined
     */
    bool HasDeviceContextForTest(int64_t deviceId) const;

    /**
     * @brief Test helper: Check if a client is associated with a device
     * @param deviceId The device ID to check
     * @param clientId The client ID to check
     * @return true if client is associated with device, false otherwise
     * @note Only available when UNIT_TEST_SUPPORT is defined
     */
    bool HasClientForDeviceForTest(int64_t deviceId, uint32_t clientId) const;
#endif

private:
    void ClosePortforDevice(
        uint32_t clientId, int64_t deviceId, std::shared_ptr<DeviceClientContext> deviceClientContext);
    int32_t CloseInputPortInner(uint32_t clientId, int64_t deviceId, uint32_t portIndex);
    void HandleBleOpenComplete(const std::string &address, bool success, int64_t deviceId,
        const std::map<int32_t, std::string> &deviceInfo);

    void ScheduleUnloadTask();
    void CancelUnloadTask();
    int32_t CloseOutputPortInner(uint32_t clientId, int64_t deviceId, uint32_t portIndex);
    std::unordered_map<int64_t, std::shared_ptr<DeviceClientContext>> deviceClientContexts_;
    std::unordered_map<int32_t, sptr<MidiInServer>> clients_;

    // Map Address -> DeviceId (For quickly checking if a BLE address is already active)
    std::unordered_map<std::string, int64_t> activeBleDevices_;

    // Map Address -> List of waiting clients
    std::unordered_map<std::string, std::list<PendingBleConnection>> pendingBleConnections_;

    std::shared_ptr<MidiDeviceManager> deviceManager_;
    static std::atomic<uint32_t> currentClientId_;
    mutable std::mutex lock_;
    int64_t unloadDelayTime_;  // Runtime-configurable unload delay (ms)

    std::atomic<bool> isUnloadPending_{false};
    std::mutex unloadMutex_;
    std::condition_variable unloadCv_;
    std::thread unloadThread_;
};
}  // namespace MIDI
}  // namespace OHOS

#endif