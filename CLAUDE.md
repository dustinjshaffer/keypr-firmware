# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Interaction Preferences

Always use the AskUserQuestion tool (question UI) when asking questions or seeking clarification, rather than asking in plain text.

## Build Commands

This is an Arduino project for ESP32-C3. Build using Arduino IDE or Arduino CLI:

1. Open `keypr-firmware.ino` in Arduino IDE
2. Select board: **ESP32C3 Dev Module**
3. Settings:
   - USB CDC On Boot: Enabled
   - Upload Speed: 921600
   - **Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**
4. Compile and upload

**IMPORTANT:** The firmware requires the `min_spiffs` partition scheme for OTA support. This provides two 1.9MB app partitions for A/B OTA updates. The default partition scheme will not fit the firmware with OTA capability.

**Required Libraries** (install via Library Manager):
- NimBLE-Arduino
- GxEPD2
- Adafruit GFX Library
- ESP32Servo
- ArduinoJson

Serial monitor: 115200 baud

No automated tests or linting configured.

## Architecture

**Keypr** is a Bluetooth-controlled time-lock box. The ESP32 device is part of a three-component system:
- **ESP32 device** (this firmware) - Smart peripheral
- **Flutter mobile app** - User interface
- **Backend API** (getkeypr.com) - Timer authority

The device receives commands via BLE, enforces lock timers locally, and displays status on e-paper.

### State Machine

The lock has seven states:
- `STATE_READY` - Servo engaged, no timer, button opens lid
- `STATE_OPEN` - Lid is open, servo retracted
- `STATE_LOCKED` - Timer active, servo engaged, cannot open
- `STATE_UNLOCKING` - Timer expired, servo released, waiting for lid to open
- `STATE_VERIFYING` - Checking with API for unlock permission
- `STATE_OFFLINE_COUNTDOWN` - API unreachable, 15-minute fallback countdown
- `STATE_LOW_BATTERY` - Critical battery state

State transitions:
```
READY → (LOCK:N) → LOCKED → (timer expires + button) → VERIFYING → (API allows) → UNLOCKING → (lid opens) → OPEN → (lid closes) → READY
```

If API unreachable during VERIFYING → OFFLINE_COUNTDOWN → (15 min or API reconnect) → UNLOCKING

### Code Organization

Single-file firmware (`keypr-firmware.ino`, ~1850 lines) with logical sections:

| Section | Description |
|---------|-------------|
| Lines 35-55 | Pin definitions |
| Lines 62-80 | BLE UUIDs and API configuration |
| Lines 82-95 | State enum definitions |
| Lines 97-160 | Global variables (WiFi, API, tamper, etc.) |
| Lines 225-380 | BLE callbacks (commands, WiFi config, device key) |
| Lines 385-530 | BLE setup and status notification |
| Lines 535-815 | Display functions |
| Lines 950-1050 | Servo control and lock/unlock functions |
| Lines 1150-1200 | Reed switch and button handling |
| Lines 1255-1545 | Storage, WiFi, and API functions |
| Lines 1545-1665 | Emergency unlock and tamper detection |
| Lines 1670-1855 | Main setup and loop |

### BLE Protocol

Custom BLE service with 9 characteristics (UUIDs defined in SHARED_API_CONTRACT.md):
- **Lock Command** (Write) - `LOCK:<minutes>`, `UNLOCK`, `STATUS`, `SYNC`, `CLEAR_TAMPER`
- **Status** (Read/Notify) - JSON with state, timer, lid, battery, tamper, OTA progress
- **Display Text** (Write) - Keyholder message (max 64 chars)
- **Device Info** (Read) - JSON with MAC, firmware version, serial
- **OTA Control** (Write/Notify) - `OTA_START:<size>:<crc>`, `OTA_ABORT`, `OTA_VERIFY`, `OTA_APPLY`, `OTA_CONFIRM`
- **Device Key** (Write) - `KEY:<base64-key>` for API authentication
- **OTA Data** (Write) - Raw firmware binary chunks (512 bytes each)
- **Time Sync** (Write) - `TIME:<unix_timestamp>` for event timestamp accuracy
- **Buffered Events** (Read/Write) - Retrieve/clear events that occurred while app disconnected

### Hardware Pins (ESP32-C3)

| Function | GPIO |
|----------|------|
| E-Ink DC | 3 |
| E-Ink CS | 4 |
| E-Ink RST | 5 |
| E-Ink BUSY | 6 |
| E-Ink CLK | 7 |
| E-Ink DIN | 10 |
| Servo | 8 |
| Reed Switch | 1 |
| Unlock Button | 2 |

Note: GPIO9 is reserved for ESP32-C3 bootstrapping; reed switch was moved to GPIO1.

## Key Documentation

- `SHARED_API_CONTRACT.md` - **Shared contract between all three Keypr projects** (symlinked). Update the BLE Protocol section when defining or changing characteristics/commands.
- `docs/team-esp32-summary.md` - Device developer guide with full BLE/WiFi protocol specs
- `docs/prd-master-overview.md` - System-wide architecture and requirements
- `README.md` - Hardware setup and pin mappings

### Contract Sync Protocol

**At the start of each session**, read `SHARED_API_CONTRACT.md` and check the Changelog for updates from Mobile App Claude or API Claude. Look for:
- New or changed BLE characteristics/commands
- Updated state definitions or terminology
- New API endpoints the device should call
- Firmware versioning requirements

If changes affect firmware, either implement them or challenge them per the Cross-Project Collaboration guidelines below.

### Cross-Project Collaboration

When reviewing `SHARED_API_CONTRACT.md`, if a specification or command doesn't align with ESP32 constraints or isn't feasible to implement in firmware:

1. **Challenge it** - Add a note to the Changelog explaining the issue
2. **Propose alternatives** - Suggest what IS possible given hardware/firmware limitations
3. **Tag other instances** - Use format: `Firmware Claude: [issue description]` so Mobile App Claude and API Claude see the feedback

Examples of firmware constraints to consider:
- Flash/RAM limits (~1.9MB per app partition with min_spiffs, ~320KB RAM)
- BLE MTU size (default ~23 bytes, can negotiate up to ~512)
- JSON parsing overhead (ArduinoJson memory allocation)
- OTA chunk size is 512 bytes per BLE write
- E-ink refresh time (~2-3 seconds for full refresh)
- OTA requires min_spiffs partition scheme for dual app partitions

## Incomplete Features

Battery monitoring is stubbed (always returns 100%) - requires voltage divider circuit.

API endpoints are documented in SHARED_API_CONTRACT.md but not yet implemented on the backend.

## Pending Improvements

- **Startup lid check:** On boot, check if lid is closed and engage servo to locked position. Currently the servo only engages when entering LOCKED state or when lock state is restored from NVS. If the device reboots in READY state with lid closed, servo should still engage to secure the lid.

- **Battery power setup:** Hardware and firmware changes needed for battery operation:

  **Hardware wiring (components: 3.7V LiPo, TP4056 charger, MT3608 boost converter):**
  ```
  LiPo Battery ──► TP4056 (B+/B-) ──► MT3608 (VIN+/VIN-) ──► ESP32-C3 (5V/GND)
  ```
  - TP4056 OUT+ → MT3608 VIN+
  - TP4056 OUT- → MT3608 VIN- (GND)
  - MT3608 VOUT+ → ESP32-C3 5V pin
  - MT3608 VOUT- → ESP32-C3 GND pin
  - **IMPORTANT:** Adjust MT3608 potentiometer to output exactly 5.0V before connecting to ESP32

  **Firmware changes needed for battery monitoring:**
  - Add voltage divider circuit: Battery+ → 100kΩ → ADC pin → 100kΩ → GND
  - Use GPIO0 or GPIO1 for ADC input (check available pins)
  - Implement `readBatteryVoltage()` using `analogRead()`
  - Convert ADC reading to percentage (4.2V=100%, 3.0V=0%)
  - Update `batteryPercent` variable (currently hardcoded to 100)
