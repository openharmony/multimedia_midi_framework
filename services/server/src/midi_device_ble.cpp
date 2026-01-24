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
    static constexpr const char *MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
    static constexpr const char *MIDI_CHAR_UUID = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
}

static BleMidiTransportDeviceDriver *instance;

static void ConvertUmpToMidi1(const uint32_t* umpData, size_t count, std::vector<uint8_t>& midi1Bytes)
{
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

static void NotifyManager(int32_t clientId, bool success) {
    if (!instance) return;
    std::string name = ""; // Could fetch name from GATT
    
    auto it = instance->devices_.find(clientId);
        
    CHECK_AND_RETURN(it != instance->devices_.end());
    
    // Use a copy of callback to avoid holding lock while calling
    auto &d = it->second;
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

static BtUuid MakeBtUuid(const std::string &s, std::string &storage)
{
    storage = s;
    BtUuid u;
    u.uuid = storage.data();
    u.uuidLen = storage.size();
    return u;
}

static bool ParseMac(const std::string &mac, BdAddr &out)
{
    CHECK_AND_RETURN_RET(mac.size() == 17, false);
    int32_t  bi = 0;
    for (size_t i = 0; i < mac.size();) {
        CHECK_AND_RETURN_RET(i + 1 < mac.size(), false);
        char c1 = mac[i];
        char c2 = mac[i+1];
        auto hexVal = [](char c)->int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return 10 + (c -'A');
            if (c >= 'a' && c <= 'f') return 10 + (c -'a');
            return -1;
        };
        int32_t v1 = hexVal(c1);
        int32_t v2 = hexVal(c2);
        CHECK_AND_RETURN_RET(v1 >= 0 && v2>= 0, false);
        out.addr[bi++] = static_cast<unsigned char>((v1 <<4) | v2);
        i += 2;
        CHECK_AND_RETURN_RET(bi != 6, true);
        CHECK_AND_CONTINUE(i < mac.size());
        CHECK_AND_RETURN_RET(mac[i] == ':', false);
        i++;
    }
    return bi == 6;
}

static bool BtUuidEquals(const BtUuid &u, const char *canonical)
{
    CHECK_AND_RETURN_RET(u.uuid && canonical, false);
    size_t len = std::strlen(canonical);
    CHECK_AND_RETURN_RET(u.uuidLen == len, false);
    for (size_t i = 0; i < len; i++) {
        unsigned char cu = (unsigned char)u.uuid[i];
        unsigned char cc = (unsigned char)canonical[i];      
        CHECK_AND_RETURN_RET(std::toupper(cu) == std::toupper(cc), false);
    }
    return true;
}

static void OnConnectionState(int32_t clientId, int32_t connState, int32_t status)
{
    CHECK_AND_RETURN(instance != nullptr);
    MIDI_INFO_LOG("client = %{public}d, connState = %{public}d, status = %{public}d", clientId, connState, status);
    
    bool isDisconnect = (connState == OHOS_STATE_DISCONNECTED) || (status != 0 && connState != OHOS_STATE_CONNECTED);

    if (isDisconnect) {
        std::unique_lock<std::mutex> lock(instance->lock_);
        auto it = instance->devices_.find(clientId);
        CHECK_AND_RETURN(it != instance->devices_.end());
        MIDI_INFO_LOG("Device disconnected or failed connection");
        
        // Notify Manager of disconnection
        // Important: Only notify if we previously notified success, 
        // OR if this is the initial failure
        lock.unlock();
        NotifyManager(clientId, false);
        lock.lock();
        BleGattcUnRegister(clientId);
        instance->devices_.erase(it);
        return;
    }

    if (connState == OHOS_STATE_CONNECTED) {
        std::lock_guard<std::mutex> lock(instance->lock_);
        auto &ctx = instance->devices_[clientId];
        ctx.connected = true;
        
        // Don't notify Manager yet. Wait for Services & Notify.
        int32_t ret = BleGattcSearchServices(clientId);
        CHECK_AND_RETURN(ret != 0);
        MIDI_ERR_LOG("Search Service failed");
        BleGattcDisconnect(clientId);
        // Will trigger callback in disconnect path or here manually if needed
    }
}

static void OnSearvicesComplete(int32_t clientId, int32_t status)
{
    CHECK_AND_RETURN(instance != nullptr);
    if (status != 0) {
        BleGattcDisconnect(clientId);
        return;
    }

    std::unique_lock<std::mutex> lock(instance->lock_);
    auto it = instance->devices_.find(clientId);
    if (it == instance->devices_.end()) return;
    auto &d = it->second;

    std::string svcStr;
    BtUuid svc = MakeBtUuid(MIDI_SERVICE_UUID, svcStr);
    
    // Simplified logic: Try to get service and subscribe
    if (BleGattcGetService(clientId, svc)) {
        d.serviceReady = true;
        d.serviceUuidStorage = MIDI_SERVICE_UUID;
        d.characteristicUuidStorage = MIDI_CHAR_UUID;
        d.dataChar.serviceUuid = MakeBtUuid(d.serviceUuidStorage, d.serviceUuidStorage);
        d.dataChar.characteristicUuid = MakeBtUuid(d.characteristicUuidStorage, d.characteristicUuidStorage);
        // Critical: Subscribe to Notify
        int32_t rc = BleGattcRegisterNotification(clientId, d.dataChar, true);
        CHECK_AND_RETURN(rc != 0);
    }
    BleGattcDisconnect(clientId);
    lock.unlock();
    NotifyManager(clientId, false);
}

static void OnRegisterNotify(int32_t clientId, int32_t status)
{
    CHECK_AND_RETURN(instance != nullptr);
    MIDI_INFO_LOG("OnRegisterNotify clientId %{public}d status %{public}d", clientId, status);

    std::unique_lock<std::mutex> lock(instance->lock_);
    auto it = instance->devices_.find(clientId);
    CHECK_AND_RETURN(it != instance->devices_.end());
    auto &d = it->second;

    if (status == 0) {
        d.notifyEnabled = true;
        MIDI_INFO_LOG("BLE MIDI Device Fully Online. Notifying Manager.");
        
        // SUCCESS! This is the only place we confirm the device is open.
    } else {
        d.notifyEnabled = false;
        MIDI_ERR_LOG("Notify Enable Failed");
        BleGattcDisconnect(clientId);
    }
    lock.unlock();
    NotifyManager(clientId, d.notifyEnabled);
}

static void OnNotification(int32_t clientId, BtGattReadData* data, int32_t status)
{
    CHECK_AND_RETURN(instance != nullptr && status == 0 && data);
    const BtGattCharacteristic &ch = data->attribute.characteristic;
    CHECK_AND_RETURN(BtUuidEquals(ch.serviceUuid, MIDI_SERVICE_UUID) &&
        BtUuidEquals(ch.characteristicUuid, MIDI_CHAR_UUID));
    int64_t devId = 0;
    UmpInputCallback cb = nullptr;
    {
        std::lock_guard<std::mutex> lock(instance->lock_);
        for (auto &[id, d] : instance->devices_) {
            CHECK_AND_CONTINUE(d.id == clientId && d.inputOpen && d.notifyEnabled);
            devId = id;
            cb = d.inputCallback;
            break;
        }
    }
    CHECK_AND_RETURN(cb && devId != 0);
    const uint8_t* src = data->data;
    size_t srcLen = data->dataLen;
    CHECK_AND_RETURN(src && srcLen !=0);
    std::ostringstream midiStream;
    for (size_t i = 0; i < static_cast<size_t>(srcLen); i++) {
        midiStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<uint32_t>(src[i]) << " ";
    }
    MIDI_INFO_LOG("midiStream 1.0: %{public}s", midiStream.str().c_str());
    
    UmpProcessor processor;
    std::vector<UmpPacket> results;
    processor.ProcessBytes(src, srcLen, [&](const UmpPacket& p){
        results.push_back(p);
    });
    for (auto p : results)  {
        std::ostringstream umpStream;
        for (uint8_t i = 0; i < p.WordCount(); i++) {
            umpStream << std::hex << std::setw(8) << std::setfill('0') << p.Word(i) << " ";
        }
        MIDI_INFO_LOG("umpStream 1.0: %{public}s", umpStream.str().c_str());
    }

    std::vector<MidiEventInner> events;
    std::vector<uint32_t> midi2;
    for (auto p : results)  {
        for (uint8_t i = 0; i < p.WordCount(); i++) {
            midi2.push_back(p.Word(i));
        }
    }
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
    return;
}

BleMidiTransportDeviceDriver::BleMidiTransportDeviceDriver()
{
    instance = this;
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
    instance = nullptr;
}

std::vector<DeviceInformation> BleMidiTransportDeviceDriver::GetRegisteredDevices()
{
    std::lock_guard<std::mutex> lock(lock_);
    std::vector<DeviceInformation> deviceInfos;
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
    }
    return deviceInfos;
}

int32_t BleMidiTransportDeviceDriver::CloseDevice(int64_t deviceId)
{
    std::unique_lock<std::mutex> lock(lock_);

    auto it = devices_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != devices_.end(), -1,"Device not found: %{public}ld", deviceId);
    DeviceCtx& ctx = it->second;

    int32_t ret = BleGattcDisconnect(ctx.id);
    MIDI_INFO_LOG("BleGattcDisconnect : %{public}d", ret);
    BleGattcUnRegister(ctx.id);
    MIDI_INFO_LOG("Unregistered client: %{public}ld", ctx.id);
    lock.unlock();
    NotifyManager(deviceId, false);
    lock.lock();
    devices_.erase(it);
    MIDI_INFO_LOG("Device closed successfully: id=%{public}ld, address=%{public}s", 
            deviceId, ctx.address.c_str());
    return 0;
}

int32_t BleMidiTransportDeviceDriver::OpenDevice(int64_t deviceId)
{
    return -1;
}

int32_t BleMidiTransportDeviceDriver::OpenDevice(std::string deviceAddr, BleDriverCallback deviceCallback)
{
    std::lock_guard<std::mutex> lock(lock_);
    
    // Check if address already exists
    for (auto &[id, d] : devices_) {
        if (d.address == deviceAddr) {
            MIDI_WARNING_LOG("Driver: Device %{public}s already has context", deviceAddr.c_str());
            // If it's fully ready, we might callback immediately, 
            // but Controller handles "Pending" logic usually.
            return -1; 
        }
    }

    DeviceCtx ctx{};
    ctx.address = deviceAddr;
    ctx.deviceCallback = deviceCallback;

    std::string uuidStorage;
    BtUuid appUuid = MakeBtUuid("00000000-0000-0000-0000-000000000001", uuidStorage);
    
    int32_t clientId = BleGattcRegister(appUuid);
    if (clientId <= 0) return -1;

    ctx.id = clientId;
    devices_[clientId] = ctx;

    BdAddr bd{};
    if (!ParseMac(deviceAddr, bd)) {
        BleGattcUnRegister(clientId);
        devices_.erase(clientId);
        return -1;
    }

    if (BleGattcConnect(clientId, &gattCallbacks_, &bd, false, OHOS_BT_TRANSPORT_TYPE_LE) != 0) {
        BleGattcUnRegister(clientId);
        devices_.erase(clientId);
        return -1;
    }
    
    return 0; // Async process started
}

int32_t BleMidiTransportDeviceDriver::OpenInputPort(int64_t deviceId, size_t portIndex, UmpInputCallback cb)
{
    CHECK_AND_RETURN_RET(portIndex == 1, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        // CHECK_AND_RETURN_RET_LOG(d.connected && d.serviceReady, -1, "not ready");
        CHECK_AND_RETURN_RET_LOG(!d.inputOpen, -1 , "already open");
        d.inputCallback = cb;
        d.inputOpen = true;
        return 0;
    }
    return -1;
}

int32_t BleMidiTransportDeviceDriver::CloseInputPort(int64_t deviceId, size_t portIndex)
{
    CHECK_AND_RETURN_RET(portIndex == 1, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        // CHECK_AND_RETURN_RET_LOG(d.connected && d.serviceReady, -1, "not ready");
        CHECK_AND_RETURN_RET_LOG(d.inputOpen, -1 , "not open");
        d.inputCallback = nullptr;
        d.inputOpen = false;
        return 0;
    }
    return -1;
}

int32_t BleMidiTransportDeviceDriver::OpenOutputPort(int64_t deviceId, size_t portIndex)
{
    CHECK_AND_RETURN_RET(portIndex == 0, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        // CHECK_AND_RETURN_RET_LOG(d.connected && d.serviceReady, -1, "not ready");
        CHECK_AND_RETURN_RET_LOG(!d.inputOpen, -1 , "already open");
        d.outputOpen = true;
        return 0;
    }
    return -1;
}

int32_t BleMidiTransportDeviceDriver::CloseOutputPort(int64_t deviceId, size_t portIndex)
{
    CHECK_AND_RETURN_RET(portIndex == 0, -1);
    std::lock_guard<std::mutex> lock(lock_);
    for (auto &[id, d] : devices_) {
        CHECK_AND_CONTINUE(d.id == deviceId);
        // CHECK_AND_RETURN_RET_LOG(d.connected && d.serviceReady, -1, "not ready");
        CHECK_AND_RETURN_RET_LOG(d.inputOpen, -1 , "not open");
        d.outputOpen = false;
        return 0;
    }
    return -1;
}

int32_t BleMidiTransportDeviceDriver::HanleUmpInput(int64_t deviceId, size_t portIndex, MidiEventInner list)
{
    CHECK_AND_RETURN_RET(portIndex == 0, -1);
    std::lock_guard<std::mutex> lock(lock_);
    auto it = devices_.find(deviceId);
    CHECK_AND_RETURN_RET_LOG(it != devices_.end(), -1, "Device not found: %{public}ld", deviceId);
    auto &d = it->second;
    CHECK_AND_RETURN_RET_LOG(d.outputOpen && d.connected && d.serviceReady, -1, "not open");

    std::vector<uint8_t> midi1Buffer;
    ConvertUmpToMidi1(list.data, list.length, midi1Buffer);
    CHECK_AND_RETURN_RET_LOG(!midi1Buffer.empty(), -1, "midi1Buffer is empty");
    const char *payload = reinterpret_cast<const char*>(midi1Buffer.data());
    int32_t payloadLen = static_cast<int32_t>(midi1Buffer.size());
    CHECK_AND_RETURN_RET_LOG(BleGattcWriteCharacteristic(d.id, d.dataChar, OHOS_GATT_WRITE_NO_RSP,
        payloadLen, payload) == 0, -1, "write characteristic failed");
    return 0;
}

} // namespace MIDI
} // namespace OHOS