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

#ifndef MIDI_DEVICE_MANANGER_H
#define MIDI_DEVICE_MANANGER_H
#include <vector>
#include <unordered_map>
#include "midi_device_connection.h"
#include "midi_device_driver.h"
#include "midi_info.h"
#include "common_event_manager.h"
#include "common_event_support.h"

namespace OHOS {
namespace MIDI {
struct DevicePortContext {
    int64_t portId;
    std::vector<int32_t> clients;
    DevicePortContext(int64_t id = 0) : portId(id) {}
};

class EventSubscriber : public EventFwk::CommonEventSubscriber {
public:
    explicit EventSubscriber(const EventFwk::CommonEventSubscribeInfo &subscribeInfo, 
                           std::function<void()> callback)
        : EventFwk::CommonEventSubscriber(subscribeInfo), callback_(std::move(callback)) {}
    void OnReceiveEvent(const EventFwk::CommonEventData &data) override;
    
private:
    std::function<void()> callback_;
};

class MidiDeviceManager {
public:
    MidiDeviceManager();
    ~MidiDeviceManager();
    void Init();
    std::vector<DeviceInformation> GetDevices();
    std::vector<PortInformation> GetDevicePorts(int64_t deviceId);
    void UpdateDevices();
    bool OpenDevice(int64_t deviceId);
    bool CloseDevice(int64_t deviceId);
    bool OpenInputPort(std::shared_ptr<DeviceConnectionForInput> &inputConnection,
                        int64_t deviceId, uint32_t portIndex);
    bool CloseInputPort(int64_t deviceId, uint32_t portIndex);
private:
    int64_t GenerateDeviceId();
    int64_t GetOrCreateDeviceId(int64_t driverDeviceId, DeviceType type);

    DeviceInformation GetDeviceForDeviceId(int64_t deviceId);
    MidiDeviceDriver *GetDriverForDeviceType(DeviceType type);
    void CompareDevices(const std::vector<DeviceInformation>& oldDevices, 
        const std::vector<DeviceInformation>& newDevices);
    std::unordered_map<DeviceType, std::unique_ptr<MidiDeviceDriver>> drivers_;
    std::vector<DeviceInformation> devices_{};
    std::shared_ptr<EventSubscriber> eventSubscriber_{nullptr};
    std::unordered_map<int64_t, int64_t> driverIdToMidiId_;

    std::atomic<int64_t> nextDeviceId_{1000};
    std::mutex devicesMutex_;
    std::mutex driversMutex_;
    std::mutex mappingMutex_;
};
} // namespace MIDI
} // namespace OHOS
#endif
