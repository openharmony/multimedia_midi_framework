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
#ifndef LOG_TAG
#define LOG_TAG "MidiDeviceManager"
#endif
#include "usb_srv_client.h"
#include "midi_service_controller.h"
#include "midi_device_mananger.h"
#include "midi_device_usb.h"
 #include "midi_device_ble.h"
#include "midi_log.h"

namespace OHOS {
namespace MIDI {
namespace {
constexpr int32_t AUDIO_CLASS_ID = 1;
constexpr int32_t MIDI_SUBCLASS_ID = 3;
}  // namespace

static std::shared_ptr<EventSubscriber> SubscribeCommonEvent(std::function<void()> callback);

MidiDeviceManager::MidiDeviceManager() : eventSubscriber_(nullptr)
{
    MIDI_INFO_LOG("MidiDeviceManager constructor");
    drivers_.emplace(DeviceType::DEVICE_TYPE_USB, std::make_unique<UsbMidiTransportDeviceDriver>());
    drivers_.emplace(DeviceType::DEVICE_TYPE_BLE, std::make_unique<BleMidiTransportDeviceDriver>());
}

MidiDeviceManager::~MidiDeviceManager()
{
    // 清理所有驱动
    {
        std::lock_guard<std::mutex> lock(driversMutex_);
        drivers_.clear();
    }
    MIDI_INFO_LOG("MidiDeviceManager destructor");
}

std::map<int32_t, std::string> MidiDeviceManager::ConvertDeviceInfo(const DeviceInformation &d) {
    std::map<int32_t, std::string> info;
    info[DEVICE_ID] = std::to_string(d.deviceId);
    info[DEVICE_TYPE] = std::to_string(d.deviceType);
    info[MIDI_PROTOCOL] = std::to_string(d.transportProtocol);
    info[PRODUCT_NAME] = d.productName;
    info[VENDOR_NAME] = d.vendorName;
    info[ADDRESS] = d.address;
    return info;
}

int64_t MidiDeviceManager::GenerateDeviceId()
{
    return ++nextDeviceId_;
}

int64_t MidiDeviceManager::GetOrCreateDeviceId(int64_t driverDeviceId, DeviceType type)
{
    std::lock_guard<std::mutex> lock(mappingMutex_);

    auto it = driverIdToMidiId_.find(driverDeviceId);  // todo sessionId
    CHECK_AND_RETURN_RET(it == driverIdToMidiId_.end(), it->second);
    int64_t deviceId = GenerateDeviceId();
    driverIdToMidiId_[driverDeviceId] = deviceId;
    return deviceId;
}

void MidiDeviceManager::Init()
{
    MIDI_INFO_LOG("Initialize");
    CHECK_AND_RETURN_LOG(eventSubscriber_ == nullptr, "feventSubscriber_ already exists");
    std::weak_ptr<MidiDeviceManager> weakSelf = shared_from_this();
    auto eventCallback = [weakSelf]() {
        auto self = weakSelf.lock();
        CHECK_AND_RETURN_LOG(self != nullptr, "MidiDeviceManager destroyed");
        self->UpdateDevices();
    };
    eventSubscriber_ = SubscribeCommonEvent(eventCallback);  // todo 工厂
    UpdateDevices();
    MIDI_INFO_LOG("MidiDeviceManager initialized successfully");
}

static std::shared_ptr<EventSubscriber> SubscribeCommonEvent(std::function<void()> callback)
{
    EventFwk::MatchingSkills matchingSkills;
    matchingSkills.AddEvent(EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_ATTACHED);
    matchingSkills.AddEvent(EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_DETACHED);
    EventFwk::CommonEventSubscribeInfo subscribeInfo(matchingSkills);
    subscribeInfo.SetThreadMode(EventFwk::CommonEventSubscribeInfo::COMMON);
    auto subscriber = std::make_shared<EventSubscriber>(subscribeInfo, callback);
    auto ret = EventFwk::CommonEventManager::NewSubscribeCommonEvent(subscriber);
    CHECK_AND_RETURN_RET_LOG(ret == MIDI_STATUS_OK, nullptr, "NewSubscribeCommonEvent Failed. ret=%{public}d", ret);
    return subscriber;
}

static bool isMidiDevice(USB::UsbDevice &usbDevice)
{
    for (auto &usbConfig : usbDevice.GetConfigs()) {
        for (auto &usbInterface : usbConfig.GetInterfaces()) {
            CHECK_AND_RETURN_RET(
                usbInterface.GetClass() != AUDIO_CLASS_ID || usbInterface.GetSubClass() != MIDI_SUBCLASS_ID, true);
        }
    }
    return false;
}

void EventSubscriber::OnReceiveEvent(const EventFwk::CommonEventData &data)
{
    std::string action = data.GetWant().GetAction();
    MIDI_INFO_LOG("OnReceiveEvent Entry. action=%{public}s", action.c_str());

    CHECK_AND_RETURN_LOG(action == EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_ATTACHED ||
                             action == EventFwk::CommonEventSupport::COMMON_EVENT_USB_DEVICE_DETACHED,
        "Unknown event action: %{public}s",
        action.c_str());
    std::string devStr = data.GetData();
    CHECK_AND_RETURN_LOG(!devStr.empty(), "Error: data.GetData() returns empty");
    auto *devJson = cJSON_Parse(devStr.c_str());  // todo 检查依赖
    CHECK_AND_RETURN_LOG(devJson, "Create devJson error");
    USB::UsbDevice usbDevice(devJson);
    cJSON_Delete(devJson);

    CHECK_AND_RETURN(isMidiDevice(usbDevice));
    CHECK_AND_RETURN_LOG(callback_ != nullptr, "callback_ is nullptr");
    callback_();
}

void MidiDeviceManager::UpdateDevices()
{
    MIDI_INFO_LOG("Updating MIDI devices");

    std::vector<DeviceInformation> driverDevices;
    {
        std::lock_guard<std::mutex> lock(driversMutex_);
        for (const auto &driverPair : drivers_) {
            auto &driver = driverPair.second;
            CHECK_AND_CONTINUE_LOG(driver != nullptr, "%{public}d driver is nullptr", driverPair.first);
            auto devices = driver->GetRegisteredDevices();
            driverDevices.insert(
                driverDevices.end(), std::make_move_iterator(devices.begin()), std::make_move_iterator(devices.end()));
        }
    }

    std::vector<DeviceInformation> newDevices;
    for (auto &device : driverDevices) {
        int64_t deviceId = GetOrCreateDeviceId(device.driverDeviceId, device.deviceType);
        DeviceInformation newDevice = device;
        newDevice.deviceId = deviceId;

        newDevices.push_back(newDevice);
    }

    std::vector<DeviceInformation> oldDevices;
    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        oldDevices = devices_;
        devices_ = newDevices;
    }

    CompareDevices(oldDevices, newDevices);  // todo 优化
}

void MidiDeviceManager::CompareDevices(
    const std::vector<DeviceInformation> &oldDevices, const std::vector<DeviceInformation> &newDevices)
{
    std::vector<DeviceInformation> addedDevices;
    std::vector<DeviceInformation> removedDevices;

    for (const auto &newDevice : newDevices) {
        auto it = std::find_if(oldDevices.begin(), oldDevices.end(), [&newDevice](const DeviceInformation &oldDevice) {
            return oldDevice.driverDeviceId == newDevice.driverDeviceId && oldDevice.deviceType == newDevice.deviceType;
        });
        if (it == oldDevices.end()) {
            addedDevices.push_back(newDevice);
            MIDI_INFO_LOG("Device added: midiId=%{public}" PRId64 ", driverId=%{public}" PRId64 ", name: %{public}s",
                newDevice.deviceId,
                newDevice.driverDeviceId,
                newDevice.productName.c_str());
        }
    }

    for (const auto &oldDevice : oldDevices) {
        auto it = std::find_if(newDevices.begin(), newDevices.end(), [&oldDevice](const DeviceInformation &newDevice) {
            return newDevice.driverDeviceId == oldDevice.driverDeviceId && newDevice.deviceType == oldDevice.deviceType;
        });
        if (it == newDevices.end()) {
            removedDevices.push_back(oldDevice);
            driverIdToMidiId_.erase(oldDevice.driverDeviceId);
            MIDI_INFO_LOG("Device removed: midiId=%{public}" PRId64 ", driverId=%{public}" PRId64 ", name: %{public}s",
                oldDevice.deviceId,
                oldDevice.driverDeviceId,
                oldDevice.productName.c_str());
        }
    }
    if (!addedDevices.empty()) {
        for (auto device : addedDevices) {
            MidiServiceController::GetInstance()->NotifyDeviceChange(DeviceChangeType::ADD, device);
        }
    }
    if (!removedDevices.empty()) {
        for (auto device : removedDevices) {
            MidiServiceController::GetInstance()->NotifyDeviceChange(DeviceChangeType::REMOVED, device);
        }
    }
}

std::vector<DeviceInformation> MidiDeviceManager::GetDevices()
{
    std::lock_guard<std::mutex> lock(devicesMutex_);
    return devices_;
}

DeviceInformation MidiDeviceManager::GetDeviceForDeviceId(int64_t deviceId)
{
    std::lock_guard<std::mutex> lock(devicesMutex_);

    for (const auto &device : devices_) {
        if (device.deviceId == deviceId) {
            return device;
        }
    }

    MIDI_ERR_LOG("Device not found: %{public}" PRId64, deviceId);
    return DeviceInformation();
}

MidiDeviceDriver *MidiDeviceManager::GetDriverForDeviceType(DeviceType type)
{
    std::lock_guard<std::mutex> lock(driversMutex_);
    auto it = drivers_.find(type);
    if (it != drivers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<PortInformation> MidiDeviceManager::GetDevicePorts(int64_t deviceId)
{
    auto device = GetDeviceForDeviceId(deviceId);
    if (device.deviceId == 0) {
        MIDI_ERR_LOG("Cannot get ports for non-existent device: %{public}" PRId64, deviceId);
        return {};
    }
    return device.portInfos;
}

int32_t MidiDeviceManager::OpenDevice(int64_t deviceId)
{
    MIDI_INFO_LOG("Opening device: %{public}" PRId64, deviceId);

    auto device = GetDeviceForDeviceId(deviceId);
    if (device.deviceId == 0) {
        MIDI_ERR_LOG("Device not found: midiId=%{public}" PRId64, deviceId);
        return MIDI_STATUS_UNKNOWN_ERROR;
    }

    auto driver = GetDriverForDeviceType(device.deviceType);
    if (!driver) {
        MIDI_ERR_LOG("Driver not found for device type: %{public}d", static_cast<int32_t>(device.deviceType));
        return MIDI_STATUS_UNKNOWN_ERROR;
    }

    int32_t result = driver->OpenDevice(device.driverDeviceId);
    CHECK_AND_RETURN_RET_LOG(result == MIDI_STATUS_OK, result, "Failed to open device: %{public}" PRId64, deviceId);
    MIDI_INFO_LOG("Device opened successfully: %{public}" PRId64, deviceId);
    return result;
}

int32_t MidiDeviceManager::OpenBleDevice(const std::string &address, BleOpenCallback callback)
{
    MIDI_INFO_LOG("MidiDeviceManager::OpenBleDevice %{public}s", address.c_str());
    auto driver = GetDriverForDeviceType(DEVICE_TYPE_BLE);
    if (!driver) {
        return MIDI_STATUS_UNKNOWN_ERROR;
    }
    // Define the callback that the DRIVER will call (can be called multiple times: connect, disconnect)
    std::weak_ptr<MidiDeviceManager> weakSelf = shared_from_this();
    auto driverCallback = [weakSelf, callback, address](bool connected, DeviceInformation devInfo) {
        auto self = weakSelf.lock();
        CHECK_AND_RETURN_LOG(self != nullptr, "MidiDeviceManager destroyed");
        MIDI_INFO_LOG("Driver Callback: connected=%{public}d, driverId=%{public}ld", connected, devInfo.driverDeviceId);
        int64_t driverDeviceId = devInfo.driverDeviceId;
        int64_t midiDeviceId = 0;
        bool exists = false;
        if (connected) {
            // 1. Assign System ID
            midiDeviceId = GetOrCreateDeviceId(driverDeviceId, DEVICE_TYPE_BLE);
            // 2. Update Internal List
            {
                std::lock_guard<std::mutex> lock(devicesMutex_);
                // Check if already exists to avoid duplicates
                for(auto &d : devices_) { if(d.deviceId == midiDeviceId) exists = true; }
                if (!exists) {
                    devInfo.deviceId = midiDeviceId;
                    devices_.push_back(devInfo);
                } else {
                    devInfo = GetDeviceForDeviceId(midiDeviceId);
                }
            }
        } else {
            // Handle Disconnect / Fail
            // If we have a mapped ID, we need to remove it
            std::lock_guard<std::mutex> mapLock(mappingMutex_);
            if (driverIdToMidiId_.count(driverDeviceId)) {
                midiDeviceId = driverIdToMidiId_[driverDeviceId];
                {
                    std::lock_guard<std::mutex> listLock(devicesMutex_);
                    auto it = std::remove_if(devices_.begin(), devices_.end(), 
                        [midiDeviceId](const DeviceInformation& d){ return d.deviceId == midiDeviceId; });
                    if (it != devices_.end()) {
                        exists = true;
                        devInfo = *it;
                        devices_.erase(it, devices_.end());
                    }
                }
                driverIdToMidiId_.erase(driverDeviceId);
            }
        }
        // 4. Return to Controller (Only meaningful for the initial Open request)
        // Note: Logic needs to ensure this specific callback object is valid. 
        // In a real scenario, we might only call 'callback' once for the result of the Open operation.
        // Subsequent disconnects are handled via NotifyDeviceChange.
        if (callback) {
            callback(connected, midiDeviceId, ConvertDeviceInfo(devInfo));
        }
        if (connected && !exists) {
            MidiServiceController::GetInstance()->NotifyDeviceChange(DeviceChangeType::ADD, devInfo);
        }
        if (!connected && exists) {
            MidiServiceController::GetInstance()->NotifyDeviceChange(DeviceChangeType::REMOVED, devInfo);
        }

    };
    int32_t ret = driver->OpenDevice(address, driverCallback); 
    return ret;
}

int32_t MidiDeviceManager::OpenInputPort(
    std::shared_ptr<DeviceConnectionForInput> &inputConnection, int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG("device: %{public}" PRId64 " portIndex: %{public}u", deviceId, portIndex);
    auto device = GetDeviceForDeviceId(deviceId);
    if (device.deviceId == 0) {
        MIDI_ERR_LOG("Device not found: midiId=%{public}" PRId64, deviceId);
        return MIDI_STATUS_UNKNOWN_ERROR;
    }
    auto driver = GetDriverForDeviceType(device.deviceType);
    CHECK_AND_RETURN_RET_LOG(driver != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "driver is nullptr");
    DeviceConnectionInfo info = {
        .driver = driver,
        .deviceId = deviceId,
        .direction = MidiPortDirection::INPUT,
        .portIndex = portIndex,
    };
    auto connection = std::make_shared<DeviceConnectionForInput>(info);
    CHECK_AND_RETURN_RET_LOG(connection != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "connection is nullptr");
    inputConnection = connection;
    std::weak_ptr<DeviceConnectionForInput> weakConnection = connection;
    // register DeviceConnectionForInput::HandleDeviceUmpInput
    auto ret = driver->OpenInputPort(
        device.driverDeviceId, static_cast<size_t>(portIndex), [weakConnection](std::vector<MidiEventInner> &events) {
            if (auto locked = weakConnection.lock()) {
                locked->HandleDeviceUmpInput(events);
            }
        });
    return ret;
}

int32_t MidiDeviceManager::CloseInputPort(int64_t deviceId, uint32_t portIndex)
{
    MIDI_INFO_LOG("device: %{public}" PRId64 " portIndex: %{public}u", deviceId, portIndex);
    auto device = GetDeviceForDeviceId(deviceId);
    if (device.deviceId == 0) {
        MIDI_ERR_LOG("Device not found: midiId=%{public}" PRId64, deviceId);
        return MIDI_STATUS_UNKNOWN_ERROR;
    }
    auto driver = GetDriverForDeviceType(device.deviceType);
    CHECK_AND_RETURN_RET_LOG(driver != nullptr, MIDI_STATUS_UNKNOWN_ERROR, "driver is nullptr");
    return driver->CloseInputPort(device.driverDeviceId, static_cast<size_t>(portIndex));
}

int32_t MidiDeviceManager::CloseDevice(int64_t deviceId)
{
    MIDI_INFO_LOG("Closing device: %{public}" PRId64, deviceId);

    auto device = GetDeviceForDeviceId(deviceId);
    if (device.deviceId == 0) {
        MIDI_ERR_LOG("Device not found: midiId=%{public}" PRId64, deviceId);
        return MIDI_STATUS_UNKNOWN_ERROR;
    }

    auto driver = GetDriverForDeviceType(device.deviceType);
    if (!driver) {
        MIDI_ERR_LOG("Driver not found for device type: %{public}d", static_cast<int32_t>(device.deviceType));
        return MIDI_STATUS_UNKNOWN_ERROR;
    }

    int32_t result = driver->CloseDevice(device.driverDeviceId);
    CHECK_AND_RETURN_RET_LOG(result == MIDI_STATUS_OK,
        result,
        "Failed to close device: midiId=%{public}" PRId64 ", driverId=%{public}" PRId64,
        deviceId,
        device.driverDeviceId);
    MIDI_INFO_LOG("Device closed successfully: midiId=%{public}" PRId64 ", driverId=%{public}" PRId64,
        deviceId,
        device.driverDeviceId);
    return result;
}
}  // namespace MIDI
}  // namespace OHOS