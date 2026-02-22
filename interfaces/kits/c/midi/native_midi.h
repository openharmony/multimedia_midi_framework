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
 * @brief Provides the definition of the C interface for the MIDI module.
 *
 * @since 24
 */
/**
 * @file native_midi.h
 *
 * @brief Declares MIDI related interfaces.
 *
 * This interfaces in this file are used for MIDI device management,
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
 * @brief Creates a MIDI client instance.
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
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if client is NULL.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 *     or {@link #MIDI_STATUS_TOO_MANY_CLIENTS} if creation failed due to resource limits.
 *         This occurs if the calling application exceeded its per-UID quota or the system is busy.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_Create(OH_MIDIClient **client, OH_MIDICallbacks callbacks, void *userData);

/**
 * @brief Destroys the MIDI client and releases resources.
 *
 * @param client Target client handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 *     or {@link #MIDI_STATUS_INVALID_CLIENT} if client is NULL or invalid.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @note Destroying the client automatically closes all devices and ports (anti-failure mechanism).
 *     It is recommended to close resources in reverse order (ports->devices->client) for code clarity,
 *     but this is not a mandatory requirement.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_Destroy(OH_MIDIClient *client);

/**
 * @brief Gets the number of connected MIDI devices.
 *
 * This function is used to determine the size of the buffer needed to store device information.
 *
 * @param client The MIDI client handle.
 * @param count Pointer to receive the number of devices.
 * @return {@link #MIDI_STATUS_OK} on success.
 *     or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if count is NULL.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetDeviceCount(OH_MIDIClient *client, size_t *count);

/**
 * @brief Gets the information of connected MIDI devices.
 *
 * Fills the user-allocated buffer with device information.
 *
 * @note If the actual number of connected devices exceeds 'capacity', only 'capacity' records
 *     are written to the buffer, and 'actualDeviceCount' is set to 'capacity'. The function
 *     returns {@link #MIDI_STATUS_OK} but the buffer contains partial data.
 *     If the actual number is less than or equal to 'capacity', all available device information
 *     is written, and 'actualDeviceCount' reflects the actual count.
 *
 * @param client The MIDI client handle.
 * @param infos User-allocated buffer to store device information.
 * @param capacity The maximum number of elements the 'infos' buffer can hold.
 * @param actualDeviceCount Pointer to receive the actual number of devices written to the buffer.
 * @return {@link #MIDI_STATUS_OK} on success.
 *     or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if infos or actualDeviceCount is NULL.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetDeviceInfos(OH_MIDIClient *client,
                                               OH_MIDIDeviceInformation *infos,
                                               size_t capacity,
                                               size_t *actualDeviceCount);

/**
 * @brief Opens a MIDI device.
 *
 * @param client Target client handle.
 * @param deviceId Device ID.
 * @param device Pointer to receive the device handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 *     or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 *     or {@link #MIDI_STATUS_DEVICE_ALREADY_OPEN} if device is already opened by this client.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if device is NULL, or the deviceId does not exist.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_OpenDevice(OH_MIDIClient *client, int64_t deviceId, OH_MIDIDevice **device);

/**
 * @brief Opens MIDI BLE device asynchronously.
 *
 * Initiates the opening of a Bluetooth LE MIDI device. This function returns immediately,
 * and the result is delivered via the provided callback.
 *
 * @permission ohos.permission.ACCESS_BLUETOOTH
 *
 * @param client Target client handle.
 * @param deviceAddr The MAC address of the BLE device (e.g., "AA:BB:CC:DD:EE:FF").
 * @param callback The callback function to be invoked when the open process completes.
 * @param userData User context pointer to be passed to the callback.
 * @return {@link #MIDI_STATUS_OK} if the open request was successfully dispatched.
 *     or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if deviceAddr or callback is nullptr.
 *     or {@link #MIDI_STATUS_PERMISSION_DENIED} if Bluetooth permission is missing.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if the service is unreachable.
 * @note This function triggers a BLE scan and open process which may take time.
 * @warning If Bluetooth permission is denied, the {@link #OH_MIDIClient_OnDeviceOpened} will be
 *     invoked with opened parameter set to false and device parameter set to NULL. The application
 *     should check the opened parameter before attempting to use the device handle.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_OpenBLEDevice(OH_MIDIClient *client,
                                              const char *deviceAddr,
                                              OH_MIDIClient_OnDeviceOpened callback,
                                              void *userData);

/**
 * @brief Closes the MIDI device.
 *
 * @note Closing a device automatically closes all opened ports on that device.
 *
 * @param device Target device handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 *     or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
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
 *     or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if count is NULL.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if deviceId is invalid.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetPortCount(OH_MIDIClient *client, int64_t deviceId, size_t *count);

/**
 * @brief Get the port information of a specific MIDI device.
 *
 * Fills the user-allocated buffer with port information.
 *
 * @note If the actual number of ports exceeds 'capacity', only 'capacity' records
 * are written to the buffer, and 'actualPortCount' is set to 'capacity'. The function
 * returns {@link #MIDI_STATUS_OK} but the buffer contains partial data.
 * If the actual number is less than or equal to 'capacity', all available port information
 * is written, and 'actualPortCount' reflects the actual count.
 *
 * @param client The MIDI client handle.
 * @param deviceId The target device ID.
 * @param infos User-allocated buffer to store port information.
 * @param capacity The maximum number of elements the 'infos' buffer can hold.
 * @param actualPortCount Pointer to receive the actual number of ports written to the buffer.
 * @return {@link #MIDI_STATUS_OK} on success.
 *     or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if infos or actualPortCount is NULL.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if deviceId is invalid.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIClient_GetPortInfos(OH_MIDIClient *client,
                                             int64_t deviceId,
                                             OH_MIDIPortInformation *infos,
                                             size_t capacity,
                                             size_t *actualPortCount);

/**
 * @brief Opens a MIDI input port (Receive Data).
 *
 * Registers a callback to receive MIDI data in batches.
 *
 * @param device Target device handle.
 * @param descriptor Port index and protocol configuration.
 * @param callback Callback function invoked when data is available.
 * @param userData Context pointer passed to the callback.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 *     or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 *     or {@link #MIDI_STATUS_INVALID_PORT} if the port is invalid or not an input port.
 *     or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if the port is already opened by this client.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if callback is NULL.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_OpenInputPort(
    OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor, OH_MIDIDevice_OnReceived callback, void *userData);

/**
 * @brief Opens a MIDI output port (Send Data).
 *
 * @param device Target device handle.
 * @param descriptor Port index and protocol configuration.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 *     or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 *     or {@link #MIDI_STATUS_INVALID_PORT} if the port is invalid or not a output port.
 *     or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if the port is already opened by this client.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_OpenOutputPort(OH_MIDIDevice *device, OH_MIDIPortDescriptor descriptor);

/**
 * @brief Closes the MIDI input port.
 *
 * @param device Target device handle.
 * @param portIndex Input port index to close.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 *     or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 *     or {@link #MIDI_STATUS_INVALID_PORT} if portIndex is invalid or not an open input port.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_CloseInputPort(OH_MIDIDevice *device, uint32_t portIndex);

/**
 * @brief Closes the MIDI output port.
 *
 * @param device Target device handle.
 * @param portIndex Output port index to close.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 *     or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 *     or {@link #MIDI_STATUS_INVALID_PORT} if portIndex is invalid or not an open output port.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_CloseOutputPort(OH_MIDIDevice *device, uint32_t portIndex);

/**
 * @brief Sends MIDI messages (Batch, Non-blocking & Atomic).
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
 * @param portIndex Target port index.
 * @param events Pointer to the array of events to send.
 * @param eventCount Number of events in the array.
 * @param eventsWritten Returns the number of events successfully consumed.
 * @return {@link #MIDI_STATUS_OK} if all events were written.
 *     or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 *     or {@link #MIDI_STATUS_INVALID_PORT} if portIndex is invalid, or not open.
 *     or {@link #MIDI_STATUS_WOULD_BLOCK} if buffer is full (check eventsWritten).
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if arguments are invalid.
 *     or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_Send(
    OH_MIDIDevice *device, uint32_t portIndex, OH_MIDIEvent *events, uint32_t eventCount, uint32_t *eventsWritten);

/**
 * @brief Sends a large SysEx message (Byte-Stream to UMP Helper).
 *
 * This is a utility function for applications that handle SysEx as raw byte streams(MIDI 1.0 style, F0...F7).
 * This works for both MIDI_PROTOCOL_1_0 and MIDI_PROTOCOL_2_0 sessions.
 * The underlying service handles the final conversion based on the device's actual capabilities.
 *
 * How it works:
 * 1. It automatically fragments the raw bytes into a sequence of UMP Type 3(64-bit Data Message) packets.
 * 2. It sends these packets sequentially using OH_MIDIDevice_Send.
 *
 * @warning **BLOCKING CALL**: This function executes a loop and may block if the buffer fills up.
 *
 * @param device Target device handle.
 * @param portIndex Target port index.
 * @param data Pointer to the byte stream to send.
 * @param byteSize Byte size of data.
 * @return {@link #MIDI_STATUS_OK} if all events were written.
 *     or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 *     or {@link #MIDI_STATUS_INVALID_PORT} if portIndex is invalid, or not open.
 *     or {@link #MIDI_STATUS_TIMEOUT} if the operation could not be completed within a reasonable time,
 *                                     may use OH_MIDIDevice_FlushOutputPort to reset.
 *     or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if arguments are invalid.
 * @since 24
 */
OH_MIDIStatusCode OH_MIDIDevice_SendSysEx(OH_MIDIDevice *device, uint32_t portIndex, uint8_t *data, uint32_t byteSize);

/**
 * @brief Flushes pending messages in output buffer.
 *
 * Immediately discards all MIDI events currently waiting in the output buffer
 * for the specified port. This includes events scheduled for future timestamps
 * that haven't been processed by the service yet.
 *
 * @note This function would not send "All Notes Off" messages. It simply clears the queue.
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
