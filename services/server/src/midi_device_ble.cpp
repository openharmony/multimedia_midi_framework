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
#define LOG_TAG "BleDeviceDriver"
#endif
#include <iostream>
#include <fstream>
#include <sstream>
#include "midi_log.h"
#include "midi_utils.h"
#include "midi_device_ble.h"
#include "ump_processor.h"

namespace OHOS {
namespace MIDI {
namespace {
    constexpr uint8_t UMP_MT_SYSTEM = 0x1;
    constexpr uint8_t UMP_MT_CHANNEL_VOICE = 0x2;
    constexpr uint32_t UMP_SHIFT_MT = 28;
    constexpr uint32_t UMP_SHIFT_STATUS = 16;
    constexpr uint32_t UMP_SHIFT_DATA1 = 8;
    constexpr uint32_t UMP_MASK_NIBBLE = 0xF;
    constexpr uint32_t UMP_MASK_BYTE = 0xFF;
    constexpr uint8_t STATUS_PROG_CHANGE = 0xC0;
    constexpr uint8_t STATUS_CHAN_PRESSURE = 0xD0;
    constexpr uint8_t STATUS_MASK_CMD = 0xF0;
    constexpr int64_t NSEC_PER_SEC = 1000000000;
    constexpr int32_t MIDI_BYTE_HEX_WIDTH = 2;
    static constexpr const char *MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
    static constexpr const char *MIDI_CHAR_UUID = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
    const size_t MAC_STR_LENGTH = 17;
    const size_t MAC_ADDR_BYTES = 6;
    const int32_t HEX_STEP = 2;
    const int32_t BIT_SHIFT_FOUR = 4;
    const int32_t HEX_VAL_OFFSET = 10;

    // Maximum data size to prevent memory exhaustion attacks
    constexpr size_t MAX_BLE_MIDI_DATA_SIZE = 512;
    // Maximum UMP packets to prevent integer overflow
    constexpr size_t MAX_UMP_PACKETS = 128;
    // Application UUID for BLE MIDI (standard Bluetooth MIDI UUID)
    static constexpr const char *BLE_MIDI_APP_UUID = "00000000-0000-0000-0000-000000000001";
}

static std::atomic<BleMidiTransportDeviceDriver*> instance;

static void ConvertUmpToMidi1(const uint32_t* umpData, size_t count, std::vector<uint8_t>& midi1Bytes)
{
    // Validate input parameters to prevent nullptr dereference
    if (umpData == nullptr || count == 0 || count > MAX_UMP_PACKETS) {
        MIDI_ERR_LOG("ConvertUmpToMidi1: Invalid input parameters - umpData=%{public}p, count=%{public}zu",
            static_cast<const void*>(umpData), count);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        uint32_t ump = umpData[i];
        uint8_t mt = (ump >> UMP_SHIFT_MT) & UMP_MASK_NIBBLE; // Message Type

        if (mt == UMP_MT_CHANNEL_VOICE) {
            // Type 2: MIDI 1.0 Channel Voice Messages (32-bit)
            // Format: [4b MT][4b Group][4b Status][4b Channel] [8b Note/Data1][8b Vel/Data2]
            // Note: In UMP, Status includes Channel. UMP: 0x2GSCDD
            uint8_t status = (ump >> UMP_SHIFT_STATUS) & UMP_MASK_BYTE;
            uint8_t data1 = (ump >> UMP_SHIFT_DATA1) & UMP_MASK_BYTE;
            uint8_t data2 = ump & UMP_MASK_BYTE;
            uint8_t cmd = status & STATUS_MASK_CMD;

            midi1Bytes.push_back(status);
            
            // Program Change (0xC0) and Channel Pressure (0xD0) are 2 bytes
            if (cmd == STATUS_PROG_CHANGE || cmd == STATUS_CHAN_PRESSURE) {
                midi1Bytes.push_back(data1);
            } else {
                // Note On, Note Off, Poly Pressure, Control Change, Pitch Bend are 3 bytes
                midi1Bytes.push_back(data1);
                midi1Bytes.push_back(data2);
            }
        } else if (mt == UMP_MT_SYSTEM) {
            // Type 1: System Common / Real Time Messages (32-bit)
            // Format: [4b MT][4b Group][8b Status][8b Data1][8b Data2]
            uint8_t status = (ump >> UMP_SHIFT_STATUS) & UMP_MASK_BYTE;
            uint8_t data1 = (ump >> UMP_SHIFT_DATA1) & UMP_MASK_BYTE;
            uint8_t data2 = ump & UMP_MASK_BYTE;

            midi1Bytes.push_back(status);

            switch (status) {
                case 0xF1: // MIDI Time Code Quarter Frame (2 bytes)
                case 0xF3: // Song Select (2 bytes)
                    midi1Bytes.push_back(data1);
                    break;
                case 0xF2: // Song Position Pointer (3 bytes)
                    midi1Bytes.push_back(data1);
                    midi1Bytes.push_back(data2);
                    break;
                case 0xF6: // Tune Request (1 byte)
                case 0xF8: // Timing Clock (1 byte)
                case 0xFA: // Start (1 byte)
                case 0xFB: // Continue (1 byte)
                case 0xFC: // Stop (1 byte)
                case 0xFE: // Active Sensing (1 byte)
                case 0xFF: // Reset (1 byte)
                    // No data bytes
                    break;
                default:
                    // 0xF0 (Sysex Start) and 0xF7 (Sysex End) are handled in Type 3 usually,
                    // but simple 1-packet sysex might appear here.
                    break;
            }
        }
    }
}

static int64_t GetCurNano()
{
    int64_t result = -1; // -1 for bad result.
    struct timespec time;
    clockid_t clockId = CLOCK_MONOTONIC;
    int ret = clock_gettime(clockId, &time);
    if (ret < 0) {
        MIDI_ERR_LOG("GetCurNanoTime fail, result:%{public}d", ret);
        return result;
    }
    result = (time.tv_sec * NSEC_PER_SEC) + time.tv_nsec;
    return result;
}


static std::vector<PortInformation> GetPortInfo()
{
    std::vector<PortInformation> portInfos;
    PortInformation out{};
    out.portId = 0;
    out.name = "BLE-MIDI Out";
    out.direction = PORT_DIRECTION_OUTPUT;
    out.transportProtocol = PROTOCOL_1_0;
    portInfos.push_back(out);
    PortInformation in{};
    in.portId = 1;
    in.name =  "BLE-MIDI In";
    in.direction = PORT_DIRECTION_INPUT;
    in.transportProtocol = PROTOCOL_1_0;
    portInfos.push_back(in);
    return portInfos;
}

static void NotifyManager(DeviceCtx &d, bool success)
{
    CHECK_AND_RETURN(instance.load() != nullptr);
    std::string name = ""; // Could fetch name from GATT
    auto cb = d.deviceCallback;

    CHECK_AND_RETURN(cb != nullptr);
    DeviceInformation devInfo;
    devInfo.driverDeviceId = d.id;
    devInfo.deviceType = DEVICE_TYPE_BLE;
    devInfo.transportProtocol = PROTOCOL_1_0;
    devInfo.address = d.address;
    devInfo.productName = "";
    devInfo.vendorName = "";
    devInfo.portInfos = GetPortInfo();
    cb(success, devInfo);
}

static bool g_cleanupDeviceAndNotifyFailure(std::unique_lock<std::mutex> &lock, int32_t clientId)
{
    // Load instance once to prevent TOCTOU issues
    auto *inst = instance.load();
    if (inst == nullptr) {
        lock.unlock();
        BleGattcDisconnect(clientId);
        return false;
    }
    auto it = inst->devices_.find(clientId);
    if (it != inst->devices_.end()) {
        DeviceCtx device = it->second;
        BleGattcUnRegister(clientId);
        inst->devices_.erase(it);
        lock.unlock();
        BleGattcDisconnect(clientId);
        NotifyManager(device, false);
        return true;
    }
    lock.unlock();
    BleGattcDisconnect(clientId);
    return false;
}

// MakeBtUuid - Create BtUuid from string, using provided storage for ownership
// Note: The returned BtUuid points to data within 'storage', which must outlive the BtUuid
static BtUuid MakeBtUuid(const std::string &uuidStr, std::string &storage)
{
    storage = uuidStr;
    BtUuid u;
    u.uuid = storage.data();
    u.uuidLen = storage.size();
    return u;
}

static bool ParseMac(const std::string &mac, BdAddr &out)
{
    CHECK_AND_RETURN_RET(mac.size() == MAC_STR_LENGTH, false);
    int32_t  bi = 0;
    for (size_t i = 0; i < mac.size();) {
        CHECK_AND_RETURN_RET(i + 1 < mac.size(), false);
        char c1 = mac[i];
        char c2 = mac[i + 1];
        auto hexVal = [](char c)->int {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'A' && c <= 'F') {
                return HEX_VAL_OFFSET + (c - 'A');
            }
            if (c >= 'a' && c <= 'f') {
                return HEX_VAL_OFFSET + (c - 'a');
            }
            return -1;
        };
        CHECK_AND_RETURN_RET(hexVal(c1) >= 0 && hexVal(c2) >= 0, false);
        uint32_t v1 = static_cast<uint32_t>(hexVal(c1));
        uint32_t v2 = static_cast<uint32_t>(hexVal(c2));
        out.addr[bi++] = static_cast<unsigned char>((v1 << BIT_SHIFT_FOUR) | v2);
        i += HEX_STEP;
        CHECK_AND_RETURN_RET(bi != MAC_ADDR_BYTES, true);
        CHECK_AND_CONTINUE(i < mac.size());
        CHECK_AND_RETURN_RET(mac[i] == ':', false);
        i++;
    }
    return bi == MAC_ADDR_BYTES;
}

static bool BtUuidEquals(const BtUuid &u, const char *canonical)
{
    CHECK_AND_RETURN_RET(u.uuid && canonical, false);
    // Use strnlen to limit scan length and prevent buffer overruns
    constexpr size_t MAX_UUID_LEN = 64;  // BLE UUID max length is 36 for standard format
    size_t len = strnlen(canonical, MAX_UUID_LEN);
    CHECK_AND_RETURN_RET(len < MAX_UUID_LEN && u.uuidLen == len, false);

    for (size_t i = 0; i < len; i++) {
        unsigned char cu = static_cast<unsigned char>(u.uuid[i]);
        unsigned char cc = static_cast<unsigned char>(canonical[i]);
        CHECK_AND_RETURN_RET(std::toupper(cu) == std::toupper(cc), false);
    }
    return true;
}

static void OnConnectionState(int32_t clientId, int32_t connState, int32_t status)
{
    // Load instance once to prevent TOCTOU issues
    auto *inst = instance.load();
    CHECK_AND_RETURN(inst != nullptr);
    MIDI_INFO_LOG("client = %{public}d, connState = %{public}d, status = %{public}d", clientId, connState, status);

    bool isDisconnect = (connState == OHOS_STATE_DISCONNECTED) || (status != 0 && connState != OHOS_STATE_CONNECTED);

    if (isDisconnect) {
        std::unique_lock<std::mutex> lock(inst->lock_);
        auto it = inst->devices_.find(clientId);
        // Device may have already been cleaned up by active disconnect (e.g., in OnSearvicesComplete)
        CHECK_AND_RETURN(it != inst->devices_.end());
        MIDI_INFO_LOG("Device disconnected or failed connection");
        DeviceCtx device = it->second;
        BleGattcUnRegister(clientId);
        inst->devices_.erase(it);
        lock.unlock();
        NotifyManager(device, false);
        return;
    }

    if (connState == OHOS_STATE_CONNECTED) {
        std::unique_lock<std::mutex> lock(inst->lock_);
        auto &ctx = inst->devices_[clientId];
        ctx.connected = true;

        // Don't notify Manager yet. Wait for Services & Notify.
        int32_t ret = BleGattcSearchServices(clientId);
        if (ret != 0) {
            MIDI_ERR_LOG("Search Service failed");
            g_cleanupDeviceAndNotifyFailure(lock, clientId);
        }
    }
}

static void OnSearvicesComplete(int32_t clientId, int32_t status)
{
    // Load instance once to prevent TOCTOU issues
    auto *inst = instance.load();
    CHECK_AND_RETURN(inst != nullptr);
    MIDI_INFO_LOG("OnServicesComplete: clientId=%{public}d, status=%{public}d", clientId, status);
    if (status != 0) {
        // Service discovery failed - cleanup and notify failure
        MIDI_ERR_LOG("Service discovery failed: clientId=%{public}d, status=%{public}d", clientId, status);
        std::unique_lock<std::mutex> lock(inst->lock_);
        g_cleanupDeviceAndNotifyFailure(lock, clientId);
        return;
    }
    std::unique_lock<std::mutex> lock(inst->lock_);
    auto it = inst->devices_.find(clientId);
    CHECK_AND_RETURN(it != inst->devices_.end());
    auto &d = it->second;
    // Use local temporary for service lookup (OK since BleGattcGetService is synchronous)
    std::string svcTempStorage;
    BtUuid svc = MakeBtUuid(MIDI_SERVICE_UUID, svcTempStorage);
    if (BleGattcGetService(clientId, svc)) {
        MIDI_INFO_LOG("MIDI service found: clientId=%{public}d", clientId);
        d.serviceReady = true;
        // Store UUID strings for ownership
        d.serviceUuidStorage = MIDI_SERVICE_UUID;
        d.characteristicUuidStorage = MIDI_CHAR_UUID;
        // Use dedicated storage in DeviceCtx for dataChar UUIDs
        d.dataCharServiceUuidStorage = MIDI_SERVICE_UUID;
        d.dataCharCharacteristicUuidStorage = MIDI_CHAR_UUID;
        d.dataChar.serviceUuid = MakeBtUuid(d.dataCharServiceUuidStorage, d.dataCharServiceUuidStorage);
        d.dataChar.characteristicUuid = MakeBtUuid(d.dataCharCharacteristicUuidStorage,
            d.dataCharCharacteristicUuidStorage);
        int32_t rc = BleGattcRegisterNotification(clientId, d.dataChar, true);
        if (rc != 0) {
            // Register notification failed - cleanup and notify failure
            g_cleanupDeviceAndNotifyFailure(lock, clientId);
            return;
        }
        // Wait for OnRegisterNotify callback
        lock.unlock();
    } else {
        // MIDI service not found - cleanup and notify failure
        MIDI_ERR_LOG("MIDI service not found: clientId=%{public}d", clientId);
        g_cleanupDeviceAndNotifyFailure(lock, clientId);
    }
}

static void OnRegisterNotify(int32_t clientId, int32_t status)
{
    // Load instance once to prevent TOCTOU issues
    auto *inst = instance.load();
    CHECK_AND_RETURN(inst != nullptr);
    MIDI_INFO_LOG("OnRegisterNotify clientId %{public}d status %{public}d", clientId, status);

    std::unique_lock<std::mutex> lock(inst->lock_);
    auto it = inst->devices_.find(clientId);
    CHECK_AND_RETURN(it != inst->devices_.end());
    auto &d = it->second;
    if (status == 0) {
        d.notifyEnabled = true;
        MIDI_INFO_LOG("BLE MIDI Device Fully Online. Notifying Manager.");
        // Copy device context before unlock to avoid dangling reference
        DeviceCtx device = it->second;
        lock.unlock();
        // SUCCESS! This is the only place we confirm the device is open.
        NotifyManager(device, true);
    } else {
        d.notifyEnabled = false;
        MIDI_ERR_LOG("Notify Enable Failed");
        // Cleanup and notify failure
        g_cleanupDeviceAndNotifyFailure(lock, clientId);
    }
}
static std::vector<uint32_t> ParseUmpData(const uint8_t* src, size_t srcLen)
{
    UmpProcessor processor;
    std::vector<uint32_t> midi2;
    processor.ProcessBytes(src, srcLen, [&](const UmpPacket& p) {
        for (uint8_t i = 0; i < p.WordCount(); i++) {
            midi2.push_back(p.Word(i));
        }
    });
    return midi2;
}

static void OnNotification(int32_t clientId, BtGattReadData* data, int32_t status)
{
    // Load instance once to prevent TOCTOU issues
    auto *inst = instance.load();
    CHECK_AND_RETURN(inst != nullptr && status == 0 && data);
    const BtGattCharacteristic &ch = data->attribute.characteristic;
    CHECK_AND_RETURN(BtUuidEquals(ch.serviceUuid, MIDI_SERVICE_UUID) &&
        BtUuidEquals(ch.characteristicUuid, MIDI_CHAR_UUID));

    const uint8_t* src = data->data;
    size_t srcLen = data->dataLen;
    // Validate data length to prevent memory exhaustion
    CHECK_AND_RETURN(src && srcLen > 0 && srcLen <= MAX_BLE_MIDI_DATA_SIZE);
    // Copy callback and events to avoid dangling reference
    UmpInputCallback cb = nullptr;
    {
        std::lock_guard<std::mutex> lock(inst->lock_);
        for (auto &[id, d] : inst->devices_) {
            CHECK_AND_CONTINUE(d.id == clientId && d.inputOpen && d.notifyEnabled);
            // Copy callback pointer while holding lock
            cb = d.inputCallback;
            break;
        }
    }
    CHECK_AND_RETURN(cb != nullptr);
    std::ostringstream midiStream;
    for (size_t i = 0; i < srcLen; i++) {
        midiStream << std::hex << std::setw(MIDI_BYTE_HEX_WIDTH) << std::setfill('0') <<
            static_cast<uint32_t>(src[i]) << " ";
    }
    MIDI_INFO_LOG("midiStream 1.0: %{public}s", midiStream.str().c_str());
    std::vector<MidiEventInner> events;
    std::vector<uint32_t> midi2 = ParseUmpData(src, srcLen);
    CHECK_AND_RETURN_LOG(!midi2.empty(), "Failed to parse UMP data");
    MidiEventInner event = {
        .timestamp = GetCurNano(),
        .length = midi2.size(),
        .data = midi2.data(),
    };
    events.emplace_back(event);
    cb(events);
    events.clear();
}

static void OnwriteComplete(int32_t clientId, BtGattCharacteristic *data, int32_t status)
{
    if (status != 0) {
        // Write operation failed - log the error and cleanup
        MIDI_ERR_LOG("BLE write complete failed: clientId=%{public}d, status=%{public}d", clientId, status);
    }
}

BleMidiTransportDeviceDriver::BleMidiTransportDeviceDriver()
{
    MIDI_INFO_LOG("BleMidiTransportDeviceDriver constructor");
    BleMidiTransportDeviceDriver* expected = nullptr;
    if (!instance.compare_exchange_strong(expected, this)) {
        MIDI_ERR_LOG("Instance already exists!");
        return;
    }
    gattCallbacks_.ConnectionStateCb = &OnConnectionState;
    gattCallbacks_.connectParaUpdateCb = nullptr;
    gattCallbacks_.searchServiceCompleteCb = &OnSearvicesComplete;
    gattCallbacks_.readCharacteristicCb = nullptr;
    gattCallbacks_.writeCharacteristicCb = &OnwriteComplete;
    gattCallbacks_.readDescriptorCb = nullptr;
    gattCallbacks_.writeDescriptorCb = nullptr;
    gattCallbacks_.configureMtuSizeCb = nullptr;
    gattCallbacks_.registerNotificationCb = &OnRegisterNotify;
    gattCallbacks_.notificationCb = &OnNotification;
    gattCallbacks_.serviceChangeCb = nullptr;
}

BleMidiTransportDeviceDriver::~BleMidiTransportDeviceDriver()
{
    instance.store(nullptr);
    MIDI_INFO_LOG("BleMidiTransportDeviceDriver instance destroyed");
}

std::vector<DeviceInformation> BleMidiTransportDeviceDriver::GetRegisteredDevices()
{
    MIDI_INFO_LOG("GetRegisteredDevices: enter");
    std::lock_guard<std::mutex> lock(lock_);
    std::vector<DeviceInformation> deviceInfos;
    size_t connectedCount = 0;
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.connected);
        DeviceInformation devInfo;
        devInfo.driverDeviceId = d.id;
        devInfo.deviceType = DEVICE_TYPE_BLE;
        devInfo.transportProtocol = PROTOCOL_1_0;
        devInfo.address = d.address;
        devInfo.productName = "";
        devInfo.vendorName = "";
        devInfo.portInfos = GetPortInfo();
        deviceInfos.push_back(devInfo);
        connectedCount++;
    }
    MIDI_INFO_LOG("GetRegisteredDevices: found %{public}zu connected devices", connectedCount);
    return deviceInfos;
}

int32_t BleMidiTransportDeviceDriver::CloseDevice(int64_t deviceId)
{
    std::unique_lock<std::mutex> lock(lock_);

    auto it = devices_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != devices_.end(), -1, "Device not found: %{public}" PRId64, deviceId);
    auto &ctx = it->second;
    // Copy all needed data before erase to avoid dangling references
    std::string address = ctx.address;
    int32_t clientId = static_cast<int32_t>(ctx.id);
    BleDriverCallback callback = ctx.deviceCallback;
    int32_t ret = BleGattcDisconnect(clientId);
    MIDI_INFO_LOG("BleGattcDisconnect : %{public}d", ret);
    BleGattcUnRegister(clientId);
    MIDI_INFO_LOG("Unregistered client: %{public}d", clientId);
    devices_.erase(it);
    lock.unlock();
    // Create DeviceCtx for callback after erase
    DeviceCtx device;
    device.id = clientId;
    device.address = address;
    device.deviceCallback = callback;
    NotifyManager(device, false);
    MIDI_INFO_LOG("Device closed successfully: id=%{public}" PRId64 ", address=%{public}s",
        deviceId, GetEncryptStr(address).c_str());
    return 0;
}

int32_t BleMidiTransportDeviceDriver::OpenDevice(int64_t deviceId)
{
    return -1;
}

int32_t BleMidiTransportDeviceDriver::OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback)
{
    MIDI_INFO_LOG("OpenDevice: address=%{public}s", GetEncryptStr(deviceAddr).c_str());
    std::lock_guard<std::mutex> lock(lock_);
    // Check if address already exists
    for (auto &[id, d] : devices_) {
        if (d.address == deviceAddr) {
            MIDI_WARNING_LOG("Driver: Device %{public}s already has context", GetEncryptStr(deviceAddr).c_str());
            // If it's fully ready, we might callback immediately,
            // but Controller handles "Pending" logic usually.
            return MIDI_STATUS_DEVICE_ALREADY_OPEN;
        }
    }

    DeviceCtx ctx{};
    ctx.address = deviceAddr;
    ctx.deviceCallback = deviceCallback;

    // Use standard BLE MIDI application UUID
    // Note: uuidStorage is local but BleGattcRegister is called immediately (synchronous call)
    std::string uuidStorage;
    BtUuid appUuid = MakeBtUuid(BLE_MIDI_APP_UUID, uuidStorage);
    
    int32_t clientId = BleGattcRegister(appUuid);
    if (clientId <= 0) {
        MIDI_ERR_LOG("BleGattcRegister failed for address=%{public}s", GetEncryptStr(deviceAddr).c_str());
        return -1;
    }
    MIDI_INFO_LOG("BleGattcRegister success: clientId=%{public}d, address=%{public}s",
        clientId, GetEncryptStr(deviceAddr).c_str());

    ctx.id = clientId;
    devices_[clientId] = ctx;

    BdAddr bd{};
    if (!ParseMac(deviceAddr, bd)) {
        MIDI_ERR_LOG("ParseMac failed: address=%{public}s", GetEncryptStr(deviceAddr).c_str());
        BleGattcUnRegister(clientId);
        devices_.erase(clientId);
        return MIDI_STATUS_GENERIC_INVALID_ARGUMENT;
    }

    if (BleGattcConnect(clientId, &gattCallbacks_, &bd, false, OHOS_BT_TRANSPORT_TYPE_LE) != 0) {
        MIDI_ERR_LOG("BleGattcConnect failed: clientId=%{public}d, address=%{public}s",
            clientId, GetEncryptStr(deviceAddr).c_str());
        BleGattcUnRegister(clientId);
        devices_.erase(clientId);
        return -1;
    }
    MIDI_INFO_LOG("BleGattcConnect initiated: clientId=%{public}d, address=%{public}s",
        clientId, GetEncryptStr(deviceAddr).c_str());
    return 0; // Async process started
}

int32_t BleMidiTransportDeviceDriver::OpenInputPort(int64_t deviceId, uint32_t portIndex, UmpInputCallback cb)
{
    CHECK_AND_RETURN_RET(portIndex == 1, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        CHECK_AND_RETURN_RET_LOG(!d.inputOpen, -1, "already open");
        d.inputCallback = cb;
        d.inputOpen = true;
        MIDI_INFO_LOG("OpenInputPort success: deviceId=%{public}" PRId64, deviceId);
        return 0;
    }
    MIDI_ERR_LOG("OpenInputPort failed: device not found, deviceId=%{public}" PRId64, deviceId);
    return -1;
}

int32_t BleMidiTransportDeviceDriver::CloseInputPort(int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET(portIndex == 1, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        CHECK_AND_RETURN_RET_LOG(d.inputOpen, -1, "not open");
        d.inputCallback = nullptr;
        d.inputOpen = false;
        MIDI_INFO_LOG("CloseInputPort success: deviceId=%{public}" PRId64, deviceId);
        return 0;
    }
    MIDI_ERR_LOG("CloseInputPort failed: device not found, deviceId=%{public}" PRId64, deviceId);
    return -1;
}

int32_t BleMidiTransportDeviceDriver::OpenOutputPort(int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET(portIndex == 0, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        CHECK_AND_RETURN_RET_LOG(!d.inputOpen, -1, "already open");
        d.outputOpen = true;
        MIDI_INFO_LOG("OpenOutputPort success: deviceId=%{public}" PRId64, deviceId);
        return 0;
    }
    MIDI_ERR_LOG("OpenOutputPort failed: device not found, deviceId=%{public}" PRId64, deviceId);
    return -1;
}

int32_t BleMidiTransportDeviceDriver::CloseOutputPort(int64_t deviceId, uint32_t portIndex)
{
    CHECK_AND_RETURN_RET(portIndex == 0, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        CHECK_AND_RETURN_RET_LOG(d.inputOpen, -1, "not open");
        d.outputOpen = false;
        MIDI_INFO_LOG("CloseOutputPort success: deviceId=%{public}" PRId64, deviceId);
        return 0;
    }
    MIDI_ERR_LOG("CloseOutputPort failed: device not found, deviceId=%{public}" PRId64, deviceId);
    return -1;
}

int32_t BleMidiTransportDeviceDriver::HandleUmpInput(int64_t deviceId, uint32_t portIndex,
    std::vector<MidiEventInner> &list)
{
    CHECK_AND_RETURN_RET(portIndex == 0, -1);
    int32_t clientId = -1;
    BtGattCharacteristic dataChar{};
    {
        // Scope for the lock: only protect the access to the devices_ map
        std::lock_guard<std::mutex> lock(lock_);
        auto it = devices_.find(deviceId);
        CHECK_AND_RETURN_RET_LOG(it != devices_.end(), -1, "Device not found: %{public}" PRId64, deviceId);
        const auto &d = it->second;
        CHECK_AND_RETURN_RET_LOG(d.outputOpen && d.connected && d.serviceReady, -1, "Device state invalid");
        // Copy necessary values to avoid holding the lock during I/O
        clientId = static_cast<int32_t>(d.id);
        dataChar = d.dataChar;
    }
    MIDI_DEBUG_LOG("%{public}s", DumpMidiEvents(list).c_str());
    for (auto midiEvent : list) {
        // Validate data pointer before use
        CHECK_AND_CONTINUE_LOG(midiEvent.data != nullptr, "HandleUmpInput: midiEvent.data is nullptr");
        std::vector<uint8_t> midi1Buffer;
        ConvertUmpToMidi1(midiEvent.data, midiEvent.length, midi1Buffer);
        CHECK_AND_CONTINUE_LOG(!midi1Buffer.empty(), "midi1Buffer is empty");
        const char *payload = reinterpret_cast<const char*>(midi1Buffer.data());
        int32_t payloadLen = static_cast<int32_t>(midi1Buffer.size());
        CHECK_AND_CONTINUE_LOG(BleGattcWriteCharacteristic(clientId, dataChar, OHOS_GATT_WRITE_NO_RSP,
            payloadLen, payload) == 0, "write characteristic failed");
    }
    MIDI_DEBUG_LOG("HandleUmpInput completed: deviceId=%{public}" PRId64 ", processed %{public}zu events",
        deviceId, list.size());
    return 0;
}
} // namespace MIDI
} // namespace OHOS