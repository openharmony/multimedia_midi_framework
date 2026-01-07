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
 * @file native_midi_base.h
 *
 * @brief Declare underlying data structure for Midi module.
 *
 * @library libohmidi.so
 * @syscap SystemCapability.Multimedia.Audio.Core
 * @kit AudioKit
 * @since 24
 * @version 1.0
 */
#ifndef NATIVE_MIDI_BASE_H
#define NATIVE_MIDI_BASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Midi status code enumeration
 * @since 24
 */
typedef enum {
    /**
     * @error Operation successful.
     */
    MIDI_STATUS_OK = 0,

    /**
     * @error Invalid parameter (e.g., null pointer).
     */
    MIDI_STATUS_GENERIC_INVALID_ARGUMENT,

    /**
     * @error IPC communication failure.
     */
    MIDI_STATUS_GENERIC_IPC_FAILURE,

    /**
     * @error Insufficient result space.
     * Returned when the buffer provided by the caller is too small to hold the result.
     */
    MIDI_STATUS_INSUFFICIENT_RESULT_SPACE,

    /**
     * @error Invalid client handle.
     */
    MIDI_STATUS_INVALID_CLIENT,

    /**
     * @error Invalid device handle.
     */
    MIDI_STATUS_INVALID_DEVICE_HANDLE,

    /**
     * @error Invalid port index.
     */
    MIDI_STATUS_INVALID_PORT,

    /**
     * @error Send buffer is transiently full.
     * Indicates that the shared memory buffer currently lacks space
     * The application should wait briefly or retry later.
     * Returned by non-blocking send when message cannot fit in the buffer.
     */
    MIDI_STATUS_WOULD_BLOCK,

    /**
     * @error Operation can not be handle in a resonable time.
     */
    MIDI_STATUS_TIMEOUT,

    /**
     * @error The client has reached the maximum number of open devices allowed.
     * To open a new device, the client must close an existing one first.
     */
    MIDI_STATUS_TOO_MANY_OPEN_DEVICES,

    /**
     * @error The client has reached the maximum number of open ports allowed.
     * To open a new port, the client must close an existing one first.
     */
    MIDI_STATUS_TOO_MANY_OPEN_PORTS,

    /**
     * @error The client has already opened this device.
     */
    MIDI_STATUS_DEVICE_ALREADY_OPEN,

    /**
     * @error The client has already opened this port.
     */
    MIDI_STATUS_PORT_ALREADY_OPEN,

    /**
     * @error The Midi system service has died or disconnected.
     * The client must be destroyed and recreated.
     */
    MIDI_STATUS_SERVICE_DIED,

    /**
     * @error Unknown system error.
     */
    MIDI_STATUS_UNKNOWN_ERROR = -1
} OH_MidiStatusCode;

/**
 * @brief Port direction enumeration
 * @since 24
 */
typedef enum {
    /**
     * @brief Input port (Device -> Host).
     */
    MIDI_PORT_DIRECTION_INPUT  = 1,

    /**
     * @brief Output port (Host -> Device).
     */
    MIDI_PORT_DIRECTION_OUTPUT = 2
} OH_MidiPortDirection;

/**
 * @brief Midi transport protocol semantics.
 *
 * @note **CRITICAL**: The SDK ALWAYS uses UMP (Universal Midi Packet) format for data transfer,
 * regardless of the selected protocol. This enum defines the "Behavior" and "Semantics"
 * of the connection, not the data structure.
 *
 * @since 24
 */
typedef enum {
    /**
     * @brief Legacy Midi 1.0 Semantics.
     *
     * Behavior:
     * - The service expects UMP packets strictly compatible with Midi 1.0.
     * - **MT 0x0**: Utility Messages (e.g., JR Timestamps).
     * - **MT 0x1**: System Real Time and System Common Messages.
     * - **MT 0x2**: MIDI 1.0 Channel Voice Messages (32-bit).
     * - **MT 0x3**: Data Messages (64-bit) used for SysEx (7-bit payload).
     *
     * - If the target hardware is Midi 1.0: The service converts UMP back to Byte Stream (F0...F7).
     * - If the target hardware is Midi 2.0: The service sends these packets as-is (encapsulated Midi 1.0).
     */
    MIDI_PROTOCOL_1_0 = 1,

    /**
     * @brief Midi 2.0 Semantics.
     *
     * Behavior:
     * - The service expects UMP packets leveraging Midi 2.0 features.
     * - **MT 0x4**: MIDI 2.0 Channel Voice Messages (64-bit, high resolution).
     * - **MT 0x0**: Utility Messages (JR Timestamps).
     * - **MT 0xD**: Flex Data Messages (128-bit, e.g., Text, Lyrics).
     * - **MT 0xF**: UMP Stream Messages (128-bit, Endpoint Discovery, Function Blocks).
     * - **MT 0x3 / MT 0x5**: Data Messages (64-bit or 128-bit).
     *
     * @note Fallback Policy: If this protocol is requested but the hardware only supports Midi 1.0,
     * the service will perform "Best-Effort" translation (e.g., downscaling 32-bit velocity
     * to 7-bit, converting Type 4 back to Type 2). Some data precision or message types (like Flex Data)
     * may be lost or ignored.
     */
    MIDI_PROTOCOL_2_0 = 2
} OH_MidiProtocol;

/**
 * @brief Midi Device Type
 * @since 24
 */
typedef enum {
    MIDI_DEVICE_TYPE_USB = 0,
    MIDI_DEVICE_TYPE_BLE = 1
} OH_MidiDeviceType;

/**
 * @brief Device connection state change action
 * @since 24
 */
typedef enum {
    MIDI_DEVICE_CHANGE_ACTION_CONNECTED = 0,
    MIDI_DEVICE_CHANGE_ACTION_DISCONNECTED = 1
} OH_MidiDeviceChangeAction;

/**
 * @brief Midi Event Structure (Universal)
 * Designed to handle both raw Byte Stream (Midi 1.0) and UMP.
 * @since 24
 */
typedef struct {
    /**
     * @brief Timestamp in nanoseconds.
     * Base time obtained via clock_gettime(CLOCK_MONOTONIC, &time)
     * 0 indicates "send immediately".
     */
    uint64_t timestamp;

    /**
     * @brief Number of 32-bit words in the packet.
     * e.g., 1 for Type 2/4 (64-bit messages use 2 words)
     */
    size_t length;

    /**
     * @brief Pointer to UMP data (Must be 4-byte aligned).
     * This contains the raw UMP words (uint32_t).
     */
    uint32_t *data;
} OH_MidiEvent;

/**
 * @brief Device Information
 * Used for enumeration and display.
 * @since 24
 */
typedef struct {
    /**
     * @brief Unique identifier for the Midi device.
     */
    int64_t midiDeviceId;

    /**
     * @brief Type of the device (USB, BLE, etc.).
     */
    OH_MidiDeviceType deviceType;

    /**
     * @brief The native protocol supported by the hardware.
     * - If MIDI_PROTOCOL_1_0: The device is a legacy device or currently configured as such.
     * - If MIDI_PROTOCOL_2_0: The device supports MIDI 2.0 features.
     * * @note Applications can use this to decide whether to enable high-resolution UI controls.
     */
    OH_MidiProtocol nativeProtocol;

    /**
     * @brief Product name of the device.
     */
    char productName[256];

    /**
     * @brief Vendor name of the device.
     */
    char vendorName[256];

    /**
     * @brief Physical address or unique identifier.
     */
    char deviceAddress[64];
} OH_MidiDeviceInformation;

/**
 * @brief Port Descriptor
 * @since 24
 */
typedef struct {
    /**
     * @brief The unique ID of the port within the device (index).
     */
    uint32_t portIndex;

    /**
     * @brief The requested protocol behavior for this session.
     *
     * This field dictates how the Service translates data between the App and the Hardware.
     *
     * **Compatibility Behavior:**
     *
     * 1. **Request MIDI_PROTOCOL_1_0 on a 2.0 Device**: (Safe)
     * - The service creates a virtual 1.0 view.
     * - App sends UMP Type 2 (Midi 1.0 Channel Voice).
     * - Device receives UMP Type 2.
     * - Fully compatible.
     *
     * 2. **Request MIDI_PROTOCOL_2_0 on a 1.0 Device**: (Lossy)
     * - The service creates a virtual 2.0 view.
     * - App sends UMP Type 4 (Midi 2.0 Voice).
     * - Service **Down-converts** Type 4 to Type 2 (e.g., clipping Velocity, dropping Per-Note data).
     * - **Warning**: Data precision will be lost. Advanced messages may be dropped.
     */
    OH_MidiProtocol protocol;
} OH_MidiPortDescriptor;

/**
 * @brief Port Information (Detailed)
 * Used for enumeration (contains display names).
 * @since 24
 */
typedef struct {
    /**
     * @brief The index of the port.
     */
    uint32_t portIndex;

    /**
     * @brief The ID of the device this port belongs to.
     */
    int64_t deviceId;

    /**
     * @brief Direction of the port (Input/Output).
     */
    OH_MidiPortDirection direction;

    /**
     * @brief Name of the port.
     */
    char name[64];
} OH_MidiPortInformation;

/**
 * @brief Declare the midi client
 */
typedef struct OH_MidiClientStruct OH_MidiClient;

/**
 * @brief Declare the midi device
 */
typedef struct OH_MidiDeviceStruct OH_MidiDevice;

/**
 * @brief Callback for monitoring device connection/disconnection
 *
 * @param userData User context provided during client creation.
 * @param action Device change action (Connected/Disconnected).
 * @param deviceInfo Information of the changed device.
 * @since 24
 */
typedef void (*OH_OnMidiDeviceChange)(void *userData,
                                      OH_MidiDeviceChangeAction action,
                                      OH_MidiDeviceInformation deviceInfo);

/**
 * @brief Callback for receiving Midi data (Batch Processing)
 *
 * @note The callback is invoked on a high-priority thread.
 * @note The 'events' array and its data pointers are transient and valid ONLY
 * for the duration of this callback. If you need to keep the data, copy it.
 *
 * @param userData User context provided during port opening.
 * @param events Pointer to the array of Midi events received.
 * @param eventCount The number of events in the array.
 * @since 24
 */
typedef void (*OH_OnMidiReceived)(void *userData, const OH_MidiEvent *events, size_t eventCount);

/**
 * @brief Callback for handling client-level errors
 * * Invoked when a critical error occurs in the Midi service (e.g., service crash).
 * Applications may need to recreate the client when this occurs.
 *
 * @param userData User context provided during client creation.
 * @param code The error code indicating the cause.
 * @since 24
 */
typedef void (*OH_OnMidiError)(void *userData, OH_MidiStatusCode code);

/**
 * @brief Client callbacks structure
 * @since 24
 */
typedef struct {
    /**
     * @brief Handler for device hotplug events.
     */
    OH_OnMidiDeviceChange onDeviceChange;

    /**
     * @brief Handler for critical service errors.
     */
    OH_OnMidiError onError;
} OH_MidiCallbacks;

#ifdef __cplusplus
}
#endif
/** @} */
#endif // NATIVE_MIDI_BASE_H