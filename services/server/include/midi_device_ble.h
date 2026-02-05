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
#ifndef MIDI_DEVICE_BLE_H
#define MIDI_DEVICE_BLE_H

#include <vector>
#include <mutex>
#include <unordered_map>
#include "midi_info.h"
#include "midi_device_driver.h"
#include "ohos_bt_gatt_client.h"

namespace OHOS {
namespace MIDI {

struct DeviceCtx {
    int64_t id; // Driver ID (Gatt Client ID)
    std::string address;
    bool connected{false};
    bool serviceReady{false};
    bool notifyEnabled{false}; // The source of truth for "Online"
    bool inputOpen{false};
    bool outputOpen{false};
    // Store owning strings for UUIDs to prevent dangling pointers
    std::string serviceUuidStorage;
    std::string characteristicUuidStorage;
    // Additional owning storage for BtGattCharacteristic UUID pointers
    std::string dataCharServiceUuidStorage;
    std::string dataCharCharacteristicUuidStorage;
    BtGattCharacteristic dataChar{};
    UmpInputCallback inputCallback{nullptr};
    // The callback to Manager
    BleDriverCallback deviceCallback{nullptr};
    bool initialCallbackCalled{false}; // Prevent double callbacks
};

class BleMidiTransportDeviceDriver : public MidiDeviceDriver {
public:
    BleMidiTransportDeviceDriver();
    virtual ~BleMidiTransportDeviceDriver();

    std::vector<DeviceInformation> GetRegisteredDevices() override;

    // Standard Open (not used for BLE usually)
    int32_t OpenDevice(int64_t deviceId) override;

    // BLE Specific Open
    // Note: Adjusted signature to allow passing the callback mechanism
    int32_t OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback) override;

    int32_t CloseDevice(int64_t deviceId) override;
    int32_t OpenInputPort(int64_t deviceId, uint32_t portIndex, UmpInputCallback cb) override;
    int32_t CloseInputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t OpenOutputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t CloseOutputPort(int64_t deviceId, uint32_t portIndex) override;
    int32_t HanleUmpInput(int64_t deviceId, uint32_t portIndex, std::vector<MidiEventInner> &list) override;

    // Make these accessible to C-style static callbacks
    std::mutex lock_;
    std::unordered_map<int32_t, DeviceCtx> devices_; // Key is DriverID (Client ID)
    BtGattClientCallbacks gattCallbacks_{};
};
} // namespace MIDI
} // namespace OHOS
#endif