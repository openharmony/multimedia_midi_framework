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
/**
 * @addtogroup OHMIDI
 * @{
 *
 * @brief Provide the definition of the C interface for the MIDI module.
 *
 * @since 24
 */
/**
 * @file native_midi.h
 *
 * @brief Declare MIDI related interfaces.
 *
 * This file interfaces are used for MIDI device management,
 * MIDI message sending and receiving, and device status monitoring.
 *
 * @library libohmidi.so
 * @syscap SystemCapability.Multimedia.Audio.MIDI
 * @kit AudioKit
 * @since 24
 */

#ifndef NATIVE_MIDI_H
#define NATIVE_MIDI_H

#include "native_midi_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create MIDI client instance
 *
 * @note **Resource Management & Best Practices**:
 * MIDI is a delay-sensitive system service. To ensure real-time performance (QoS)
 * and system stability, the service enforces the following limits:
 * 1. **System-wide limit**: A global maximum number of active MIDI clients allowed.
 * 2. **Per-Application limit**: A maximum number of MIDI clients allowed per App UID.
 *
 * Applications are **strongly recommended** to maintain a single `OH_MIDIClient`
 * instance throughout their lifecycle and use it to manage multiple devices/ports.
 *
 * @param client Pointer to receive the new client handle.
 * @param callbacks Callback structure for system events.
 * @param userData User context to be passed to callbacks.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds,
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if client is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * or {@link #MIDI_STATUS_TOO_MANY_CLIENTS} if creation failed due to resource limits.
 * This occurs if the calling application exceeded its per-UID quota or the system is busy.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_Create(OH_MIDIClient **client, OH_MIDICallbacks callbacks, void *userData);

/**
 * @brief Destroy MIDI client and release resources
 *
 * @param client Target client handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is NULL or invalid.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @note Destroying client automatically closes all devices and ports (anti-failure mechanism).
 * It is recommended to close resources in reverse order (ports→devices→client) for code clarity,
 * but this is not a mandatory requirement.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_Destroy(OH_MIDIClient *client);

/**
 * @brief Get the number of connected MIDI devices.
 *
 * This function is used to determine the size of the buffer needed to store device information.
 *
 * @param client The MIDI client handle.
 * @param count Pointer to receive the number of devices.
 * @return {@link #MIDI_STATUS_OK} on success.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if count is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetDeviceCount(OH_MIDIClient *client, size_t *count);

/**
 * @brief Get the information of connected MIDI devices.
 *
 * Fills the user-allocated buffer with device information.
 *
 * @note Race Condition Handling:
 * If the number of devices increases between calling OH_MIDIGetDeviceCount and this function,
 * this function will only fill up to 'capacity' devices, 'actualNumDevices' set to 'capacity'.
 * If the number decreases, it will fill the actual available devices.
 * Always check 'actualNumDevices' for the real number of records written.
 *
 * @param client The MIDI client handle.
 * @param infos User-allocated buffer to store device information.
 * @param capacity The maximum number of elements the 'infos' buffer can hold.
 * @param actualNumDevices Pointer to receive the actual number of devices written to the buffer.
 * @return {@link #MIDI_STATUS_OK} on success.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if infos or actualNumDevices is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetDeviceInfos(OH_MIDIClient *client,
                                               OH_MIDIDeviceInformation *infos,
                                               size_t capacity,
                                               size_t *actualNumDevices);

/**
 * @brief Open MIDI device
 *
 * @param client Target client handle.
 * @param deviceId Device ID.
 * @param device Pointer to receive the device handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_DEVICE_ALREADY_OPEN} if device is opened by this client.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if device is nullptr, or the deviceId does not exist.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_OpenDevice(OH_MIDIClient *client, int64_t deviceId, OH_MIDIDevice **device);

/**
 * @brief Open MIDI BLE device asynchronously.
 *
 * Initiates a connection to a Bluetooth LE MIDI device. This function returns immediately,
 * and the connection result is delivered via the provided callback.
 *
 * @permission ohos.permission.ACCESS_BLUETOOTH
 *
 * @param client Target client handle.
 * @param deviceAddr The MAC address of the BLE device (e.g., "AA:BB:CC:DD:EE:FF").
 * @param callback The callback function to be invoked when the connection process completes.
 * @param userData User context pointer to be passed to the callback.
 * @return {@link #MIDI_STATUS_OK} if the connection request was successfully dispatched.
 * {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if deviceAddr or callback is nullptr.
 * {@link #MIDI_STATUS_PERMISSION_DENIED} if Bluetooth permission is missing.
 * {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if the service is unreachable.
 * @note This function triggers a BLE scan and connection process which may take time.
 * @warning If Bluetooth permission is denied, the callback will be invoked with
 * opened=false and device=NULL. The application should check the 'opened' parameter
 * before attempting to use the device handle.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_OpenBleDevice(OH_MIDIClient *client,
                                               const char *deviceAddr,
                                               OH_MIDIClient_OnDeviceOpened callback,
                                               void *userData);

/**
 * @brief Close MIDI device
 *
 * @note Closing a device automatically closes all opened ports on that device.
 *
 * @param device Target device handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_Close(OH_MIDIDevice *device);

/**
 * @brief Get the number of ports for a specific MIDI device.
 *
 * This function is used to determine the size of the buffer needed to store port information.
 *
 * @param client The MIDI client handle.
 * @param deviceId The target device ID.
 * @param count Pointer to receive the number of ports.
 * @return {@link #MIDI_STATUS_OK} on success.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if count is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if deviceId is invalid.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetPortCount(OH_MIDIClient *client, int64_t deviceId, size_t *count);

/**
 * @brief Get the port information of a specific MIDI device.
 *
 * Fills the user-allocated buffer with port information.
 *
 * @note Race Condition Handling:
 * If the number of ports increases between calling OH_MIDIGetPortCount and this function,
 * this function will only fill up to 'capacity' ports, 'actualNumPorts' set to 'capacity'.
 * If the number decreases, it will fill the actual available ports.
 * Always check 'actualNumPorts' for the real number of records written.
 *
 * @param client The MIDI client handle.
 * @param deviceId The target device ID.
 * @param infos User-allocated buffer to store port information.
 * @param capacity The maximum number of elements the 'infos' buffer can hold.
 * @param actualNumPorts Pointer to receive the actual number of ports written to the buffer.
 * @return {@link #MIDI_STATUS_OK} on success.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if infos or actualNumPorts is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if deviceId is invalid.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetPortInfos(OH_MIDIClient *client,
                                             int64_t deviceId,
                                             OH_MIDIPortInformation *infos,
                                             size_t capacity,
                                             size_t *actualNumPorts);

/**
 * @brief Open MIDI input port (Receive Data)
 *
 * Registers a callback to receive MIDI data in batches.
 *
 * @param device Target device handle.
 * @param descriptor Port index and protocol configuration.
 * @param callback Callback function invoked when data is available.
 * @param userData Context pointer passed to the callback.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid or not a input port.
 * or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if port is already opened by this client.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if callback is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_OpenInputPort(
    OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor, OH_MIDIDevice_OnReceived callback, void *userData);

/**
 * @brief Open MIDI output port (Send Data)
 *
 * @param device Target device handle.
 * @param descriptor Port index and protocol configuration.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid or not a output port.
 * or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if port is already opened by this client.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_OpenOutputPort(OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor);

/**
 * @brief Close MIDI input port
 *
 * @param device Target device handle.
 * @param portIndex Port index.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid, or not open.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_ClosePort(OH_MIDIDevice *device, uint32_t portIndex);

/**
 * @brief Send MIDI messages (Batch, Non-blocking & Atomic)
 *
 * Attempts to write an array of events to the shared memory buffer.
 *
 * - Atomicity: Each event in the array is treated atomically.
 * It is either fully written or not written at all.
 * - Partial Success: If the buffer becomes full midway, the function returns
 * {@link #MIDI_STATUS_WOULD_BLOCK} and sets eventsWritten to the number of events
 * successfully enqueued.
 *
 * @param device Target device handle.
 * @param portIndex Target portIndex.
 * @param events Pointer to the array of events to send.
 * @param eventCount Number of events in the array.
 * @param eventsWritten Returns the number of events successfully consumed.
 * @return {@link #MIDI_STATUS_OK} if all events were written.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid, or not open.
 * or {@link #MIDI_STATUS_WOULD_BLOCK} if buffer is full (check eventsWritten).
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if arguments are invalid.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_Send(
    OH_MIDIDevice *device, uint32_t portIndex, OH_MIDIEvent *events, uint32_t eventCount, uint32_t *eventsWritten);

/**
 * @brief Send a large SysEx message (Byte-Stream to UMP Helper)
 *
 * This is a UTILITY function for applications that handle SysEx as raw byte streams(MIDI 1.0 style, F0...F7).
 * This works for BOTH MIDI_PROTOCOL_1_0 and MIDI_PROTOCOL_2_0 sessions.
 * The underlying service handles the final conversion based on the device's actual capabilities.
 *
 * How it works:
 * 1. It automatically fragments the raw bytes into a sequence of UMP Type 3(64-bit Data Message) packets.
 * 2. It sends these packets sequentially using OH_MIDISend.
 *
 * @warning **BLOCKING CALL**: This function executes a loop and may block if the buffer fills up.
 *
 * @param device Target device handle.
 * @param portIndex Target port index.
 * @param data Pointer to the array of events to send.
 * @param byteSize Number of events in the array.
 * @return {@link #MIDI_STATUS_OK} if all events were written.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid, or not open.
 * or {@link #MIDI_STATUS_TIMEOUT} could not be completed within a reasonable time,
 *                                 may use OH_MIDIFlushOutputPort to reset.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if arguments are invalid.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_SendSysEx(OH_MIDIDevice *device, uint32_t portIndex, uint8_t *data, uint32_t byteSize);

/**
 * @brief Flush pending messages in output buffer
 *
 * Immediately discards all MIDI events currently waiting in the output buffer
 * for the specified port. This includes events scheduled for future timestamps
 * that haven't been processed by the service yet.
 *
 * @note This does NOT send "All Notes Off" messages. It simply clears the queue.
 *
 * @param device Target device handle.
 * @param portIndex Target port index.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds,
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portIndex invalid or not a output port.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_FlushOutputPort(OH_MIDIDevice *device, uint32_t portIndex);

#ifdef __cplusplus
}
#endif
/** @} */
#endif // NATIVE_MIDI_H