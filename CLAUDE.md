# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is the **OpenHarmony MIDI Framework** (`midi_framework`), a system service and Native API for MIDI device management on OpenHarmony. It provides Universal MIDI Packet (UMP) based communication with USB and BLE MIDI devices, using shared memory for low-latency data transfer.

## Build Commands

### Build component
```bash
# From part_compile_ohos directory (two levels up from midi_framework)
cd ../../..
hb build midi_framework -i
```

The `hb` (Harmony Build) tool is used for component-level compilation with incremental builds (`-i` flag).

### Run tests
```bash
# Unit tests (built when testonly = true)
hb build midi_unit_test -i

# Demo test
hb build midi_demo_test -i
```

### Individual test modules
- `midi_common_unittest` - Shared ring buffer and UMP processor tests
- `midi_client_unit_test` - Client-side logic tests
- `midi_server_unit_test` - Service controller tests
- `midi_device_manager_unit_test` - Device management tests
- `ump_processor_unittest` - MIDI 1.0 to UMP conversion tests

## Architecture

### Three-Tier Structure

1. **Framework Layer** (`frameworks/native/`)
   - `ohmidi/` - Native API implementation (libohmidi.so), C interface in `interfaces/kits/c/midi/native_midi.h`
   - `midi/` - Internal client logic (`MidiClientPrivate`, `MidiDevicePrivate`)
   - `midiutils/` - Utility functions

2. **Service Layer** (`services/server/`)
   - `midi_service.cpp` - System Ability entry point (SA ID 3014, on-demand start)
   - `midi_service_controller.cpp` - Central service controller, manages clients, devices, and auto-exit
   - `midi_device_mananger.cpp` - Device enumeration and driver abstraction
   - `midi_device_usb.cpp` / `midi_device_ble.cpp` - Hardware-specific drivers
   - `midi_device_connection.cpp` - Port management and data routing
   - `midi_client_connection.cpp` - Per-client shared memory management
   - `midi_in_server.cpp` - IPC endpoint per client

3. **Common Layer** (`services/common/`)
   - `midi_shared_ring.cpp` - Lock-free ring buffer for cross-process UMP transfer
   - `ump_processor.cpp` - MIDI 1.0 byte stream to UMP conversion (for BLE)
   - `futex_tool.cpp` - Futex-based synchronization

### IPC Architecture

- **IDL Definitions** (`services/idl/`): Define client-service protocol
  - `IMidiService.idl` - Service discovery (GetService, CreateMidiInServer)
  - `IIpcMidiInServer.idl` - Per-client session interface
  - `IMidiCallback.idl` - Device change and error callbacks
  - `IMidiDeviceOpenCallback.idl` - BLE device open async result

### Data Flow: Shared Memory

Client and service communicate via **shared memory ring buffers** (`MidiSharedRing`):
- Service allocates shared memory, passes fd to client via IPC
- Zero-copy data transfer using lock-free ring buffer
- Futex-based wake-up for low-latency notification
- Support for timestamp-scheduled output

### Device Types

- **USB MIDI**: Passive - devices appear via USB hotplug, managed through ALSA driver
- **BLE MIDI**: Active - app scans via `@ohos.bluetooth.ble`, then calls `OH_MIDIClient_OpenBleDevice`
- Service asynchronously connects and delivers result via `OH_MIDIOnDeviceOpened` callback

### Service Lifecycle

- **On-demand**: Service starts when first client calls `OH_MIDIClient_Create`
- **Auto-exit**: Service exits 15 seconds after last client disconnects (configurable via `SetUnloadDelay`)
- **Death notification**: Client death detected via IPC Death Recipient, resources auto-cleaned

## Resource Limits

Service enforces resource limits (see `midi_service_controller.h`):
- `MAX_CLIENTS = 8` - Global maximum clients
- `MAX_CLIENTS_PER_APP = 2` - Per-application (UID) maximum
- `MAX_DEVICES_PER_CLIENT = 16` - Devices a single client can open
- `MAX_PORTS_PER_CLIENT = 64` - Total ports a client can open

## Key Design Patterns

- **UMP-Native**: All data is UMP (Universal MIDI Packet) format internally. MIDI 1.0 devices are wrapped in UMP Type 2/3 packets.
- **Driver Abstraction**: `MidiDeviceDriver` interface allows USB/BLE drivers to be swapped or mocked for testing
- **Unit Test Support**: `UNIT_TEST_SUPPORT` define exposes `InjectDriverForTest`, `ClearStateForTest` methods
- **Callback threading**: `OnMIDIReceived` runs on dedicated receiver thread; do not block or call UI code from callbacks

## Native API Convention

All C API functions follow `OH_MIDI*` naming:
- Return `OH_MIDIStatusCode` enum (MIDI_STATUS_OK on success)
- Handle types: `OH_MIDIClient*`, `OH_MIDIDevice*` (opaque pointers)
- Data in user-allocated buffers (call GetCount first, then GetInfos)
- Async operations use callbacks (e.g., `OH_MIDIOnDeviceOpened` for BLE)

## Dependencies

Component depends on (see `bundle.json`):
- `safwk`, `samgr` - System Ability framework
- `ipc` - IPC communication
- `usb_manager` - USB device management
- `bluetooth` - BLE connectivity
- `drivers_interface_midi` - MIDI HDI driver
- `common_event_service` - Device hotplug events
