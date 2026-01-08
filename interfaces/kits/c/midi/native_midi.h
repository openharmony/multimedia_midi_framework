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
/**
 * @addtogroup OHMIDI
 * @{
 *
 * @brief Provide the definition of the C interface for the Midi module.
 *
 * @since 24
 * @version 1.0
 */
/**
 * @file native_midi.h
 *
 * @brief Declare Midi related interfaces.
 *
 * This file interfaces are used for Midi device management,
 * Midi message sending and receiving, and device status monitoring.
 *
 * @library libohmidi.so
 * @syscap SystemCapability.Multimedia.Audio.Midi
 * @kit AudioKit
 * @since 24
 * @version 1.0
 */
#ifndef NATIVE_MIDI_H
#define NATIVE_MIDI_H

#include "native_midi_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create Midi client instance
 *
 * @param client Pointer to receive the new client handle.
 * @param callbacks Callback structure for system events.
 * @param userData User context to be passed to callbacks.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds,
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if client is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiClient_Create(OH_MidiClient **client, OH_MidiCallbacks callbacks, void *userData);

/**
 * @brief Destroy Midi client and release resources
 *
 * @param client Target client handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is NULL or invalid.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiClient_Destroy(OH_MidiClient *client);

/**
 * @brief Enumerate all Midi devices (Double Call Pattern)
 *
 * Pattern:
 * 1. Call with informations=nullptr to get the count in numDevices.
 * 2. Allocate memory.
 * 3. Call with allocated buffer to get data.
 *
 * @param client Target client handle.
 * @param infos User-allocated buffer, or nullptr.
 * @param numDevices Capacity (in) / Actual count (out).
 * @return {@link #MIDI_STATUS_OK} if execution succeeds,
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_INSUFFICIENT_RESULT_SPACE} if buffer capacity is too small.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if numDevices or infos is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiGetDevices(OH_MidiClient *client, OH_MidiDeviceInformation *infos, size_t *numDevices);

/**
 * @brief Open Midi device
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
OH_MidiStatusCode OH_MidiOpenDevice(OH_MidiClient *client, int64_t deviceId, OH_MidiDevice **device);

/**
 * @brief Open Midi BLE device
 *
 * @permission ohos.permission.ACCESS_BLUETOOTH
 * @param client Target client handle.
 * @param deviceAddr BLE Mac Address.
 * @param device Pointer to receive the device handle.
 * @param deviceId Device ID.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_DEVICE_ALREADY_OPEN} if device is opened by this client.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if device is nullptr, or the deviceAddr does not exist.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiOpenBleDevice(OH_MidiClient *client, const char *deviceAddr, OH_MidiDevice **device,
                                       int64_t *deviceId);

/**
 * @brief Close Midi device
 *
 * @param device Target device handle.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * @since 24
 */
OH_MidiStatusCode OH_MidiCloseDevice(OH_MidiDevice *device);

/**
 * @brief Get port information (Double Call Pattern)
 *
 * @param device Target device handle.
 * @param infos User-allocated buffer, or nullptr.
 * @param numPorts Capacity (in) / Actual count (out).
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_CLIENT} if client is invalid.
 * or {@link #MIDI_STATUS_INSUFFICIENT_RESULT_SPACE} if buffer capacity is too small.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if numPorts or infos is nullptr, or deviceId is invalid.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiGetDevicePorts(OH_MidiClient *client, int64_t deviceId, OH_MidiPortInformation *infos,
                                        size_t *numPorts);

/**
 * @brief Open Midi input port (Receive Data)
 *
 * Registers a callback to receive Midi data in batches.
 *
 * @param device Target device handle.
 * @param descriptor Port index and protocol configuration.
 * @param callback Callback function invoked when data is available.
 * @param userData Context pointer passed to the callback.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if port is opened by this client.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid or not a input port.
 * or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if port is already opened.
 * or {@link #MIDI_STATUS_GENERIC_INVALID_ARGUMENT} if inputHandler is nullptr.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiOpenInputPort(OH_MidiDevice *device, OH_MidiPortDescriptor portIndex,
                                       OH_OnMidiReceived callback, void *userData);

/**
 * @brief Open Midi output port (Send Data)
 *
 * @param device Target device handle.
 * @param descriptor Port index and protocol configuration.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if port is opened by this client.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid or not a output port.
 * or {@link #MIDI_STATUS_PORT_ALREADY_OPEN} if port is already opened.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiOpenOutputPort(OH_MidiDevice *device, OH_MidiPortDescriptor descriptor);

/**
 * @brief Close Midi input port
 *
 * @param device Target device handle.
 * @param portIndex Port index.
 * @return {@link #MIDI_STATUS_OK} if execution succeeds.
 * or {@link #MIDI_STATUS_INVALID_DEVICE_HANDLE} if device is invalid.
 * or {@link #MIDI_STATUS_INVALID_PORT} if portindex is invalid, or not open.
 * or {@link #MIDI_STATUS_GENERIC_IPC_FAILURE} if connection to system service fails.
 * @since 24
 */
OH_MidiStatusCode OH_MidiClosePort(OH_MidiDevice *device, uint32_t portIndex);

/**
 * @brief Send Midi messages (Batch, Non-blocking & Atomic)
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
OH_MidiStatusCode OH_MidiSend(OH_MidiDevice *device, uint32_t portIndex, OH_MidiEvent *events, uint32_t eventCount,
                              uint32_t *eventsWritten);

/**
 * @brief Send a large SysEx message (Byte-Stream to UMP Helper)
 *
 * This is a UTILITY function for applications that handle SysEx as raw byte streams(Midi 1.0 style, F0...F7).
 * This works for BOTH MIDI_PROTOCOL_1_0 and MIDI_PROTOCOL_2_0 sessions.
 * The underlying service handles the final conversion based on the device's actual capabilities.
 *
 * How it works:
 * 1. It automatically fragments the raw bytes into a sequence of UMP Type 3(64-bit Data Message) packets.
 * 2. It sends these packets sequentially using OH_MidiSend.
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
OH_MidiStatusCode OH_MidiSendSysEx(OH_MidiDevice *device, uint32_t portIndex, uint8_t *data, uint32_t byteSize);

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
OH_MidiStatusCode OH_MidiFlushOutputPort(OH_MidiDevice *device, uint32_t portIndex);

#ifdef __cplusplus
}
#endif
/** @} */
#endif // NATIVE_MIDI_H