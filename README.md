# Keypr Firmware

ESP32-C3 firmware for a BLE-controlled time-lock box with e-ink display.

## Overview

Keypr is a time-lock box that can be locked for a specified duration via Bluetooth. Once locked, it cannot be opened until the timer expires. Features include:

- BLE control from phone/companion app
- 1.54" e-ink display showing lock status and countdown
- Servo-controlled latch mechanism
- Reed switch for lid detection
- Manual unlock button
- Customizable keyholder messages

## Hardware

### Components

| Component | Model |
|-----------|-------|
| Microcontroller | ESP32-C3-DevKitC-02 v1.1 |
| Display | Waveshare 1.54" e-ink V2 (SSD1681) |
| Servo | SG90 |
| Lid Detection | Reed switch |
| Unlock | Momentary push button |

### Pin Mappings

```
ESP32-C3-DevKitC-02 v1.1

E-Ink Display (SPI):
  MOSI  → GPIO7  (J1 pin 9)
  CLK   → GPIO6  (J1 pin 8)
  CS    → GPIO10 (J3 pin 7)
  DC    → GPIO3  (J3 pin 5)
  RST   → GPIO4  (J1 pin 6)
  BUSY  → GPIO5  (J1 pin 7)

Servo:
  Signal → GPIO8 (J1 pin 11)

Reed Switch:
  Signal → GPIO9 (J1 pin 12)

Unlock Button:
  Signal → GPIO2 (J3 pin 4)
```

## BLE Interface

**Service UUID:** `12345678-1234-1234-1234-123456789abc`

### Characteristics

| Characteristic | UUID | Properties | Description |
|---------------|------|------------|-------------|
| Lock Status | `...ab1` | Read, Notify | Current lock state |
| Lock Command | `...ab2` | Write | Send commands |
| Time Left | `...ab3` | Read, Notify | Seconds remaining |
| Battery | `...ab4` | Read | Battery percentage |
| Display Text | `...ab5` | Write | Custom keyholder message |

### Commands

Write these strings to the Lock Command characteristic:

| Command | Description |
|---------|-------------|
| `LOCK:<minutes>` | Lock for specified minutes (1-525600) |
| `UNLOCK` | Unlock (only works when unlockable) |
| `STATUS` | Request status update |

### Examples

```
LOCK:60      # Lock for 1 hour
LOCK:1440    # Lock for 24 hours
LOCK:10080   # Lock for 1 week
UNLOCK       # Unlock the box
```

## State Machine

```
UNLOCKABLE → LOCKED → UNLOCKABLE
     ↓         ↑
  UNLOCKING ───┘
```

- **UNLOCKABLE**: Timer expired or never set. Button press or UNLOCK command works.
- **LOCKED**: Timer active. Cannot unlock until timer expires.
- **UNLOCKING**: Servo released. Waiting for lid open→close cycle to complete.

## Building

### Requirements

- Arduino IDE or Arduino CLI
- ESP32 board support package

### Libraries

Install via Library Manager:
- NimBLE-Arduino
- GxEPD2
- Adafruit GFX Library
- ESP32Servo

### Board Settings

- Board: "ESP32C3 Dev Module"
- USB CDC On Boot: Enabled (for serial output)
- Upload Speed: 921600

## Installation

1. Clone this repository
2. Open `sketch_jan4b.ino` in Arduino IDE
3. Install required libraries
4. Select ESP32C3 Dev Module board
5. Upload to device

## License

MIT
