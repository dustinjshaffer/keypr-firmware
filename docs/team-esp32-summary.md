# Keypr ESP32 Device Developer Summary

This document summarizes everything needed to build the Keypr ESP32 lock box device firmware.

---

## System Architecture

```
┌─────────────────┐      WiFi       ┌─────────────────┐
│    ESP32        │◄───────────────►│      API        │
│    Device       │  ← YOU ARE HERE │  (getkeypr.com) │
└────────▲────────┘                 └────────▲────────┘
         │ BLE                               │ HTTPS
         │                                   │
┌────────▼───────────────────────────────────▼────────┐
│                   Flutter App                        │
└──────────────────────────────────────────────────────┘
```

### Device Responsibilities

The ESP32 device is a **smart peripheral** that:

- Controls the physical lock mechanism (servo/solenoid)
- Communicates with the app via BLE
- Communicates with API directly via WiFi (when configured)
- Reports its actual state (locked/unlocked, lid open/closed)
- Enforces local timers when offline
- Detects and reports tamper attempts
- Displays status on e-paper screen

**Important:** The API is the source of truth. The device executes commands from the API and reports its actual state back.

---

## Hardware Specifications

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32 (BLE + WiFi capable) |
| **Display** | 1.54" e-paper |
| **Lock Mechanism** | Servo motor (or latching solenoid) |
| **Open Button** | Physical button - triggers lid open when unlocked |
| **Emergency Button** | Physical button - pattern input for emergency unlock |
| **Lid Sensor** | Reed switch - detects lid open/closed |
| **Tamper Detection** | Multiple sensors (case, force, voltage) |
| **Power** | Rechargeable LiPo + USB-C charging |
| **Battery Target** | 2+ weeks standby |

---

## Device States

### Lock State Machine

```
         ┌──────────────┐
         │   UNLOCKED   │◄────────────────┐
         └──────┬───────┘                 │
                │ LOCK command            │ UNLOCK command
                ▼                         │
         ┌──────────────┐                 │
         │    LOCKED    │─────────────────┘
         └──────────────┘
                │
                │ Emergency unlock
                ▼
         ┌──────────────┐
         │   UNLOCKED   │ (with emergency flag)
         └──────────────┘
```

### Lid State

```
CLOSED ◄──► OPEN
```

Lid can only open when device is UNLOCKED.

### Connection States

```
BLE: DISCONNECTED | CONNECTED
WiFi: NOT_CONFIGURED | CONFIGURED | CONNECTED | FAILED
```

---

## Communication Protocols

### BLE Protocol

The device exposes a BLE GATT service for app communication.

**Service UUID:** `(define unique UUID for Keypr)`

| Characteristic | UUID | Properties | Description |
|---------------|------|------------|-------------|
| Device Info | `...0001` | Read | Serial number, firmware version |
| Lock State | `...0002` | Read, Notify | Current lock state |
| Lid State | `...0003` | Read, Notify | Reed switch state |
| Battery Level | `...0004` | Read, Notify | Battery percentage |
| Command | `...0005` | Write | Receive commands |
| Command Response | `...0006` | Notify | Command acknowledgment |
| Tamper Alert | `...0007` | Notify | Tamper detection events |
| Display Message | `...0008` | Write | Text for e-paper display |
| WiFi Config | `...0009` | Write | SSID and password |
| WiFi Status | `...000A` | Read, Notify | WiFi connection status |

### Command Format (BLE)

Commands received on Command characteristic:

```json
{
  "cmd": "lock" | "unlock" | "open_lid" | "set_display" | "configure_wifi",
  "id": "unique-command-id",
  "ts": 1704567890,
  "data": { ... }  // command-specific data
}
```

### Command Response Format

```json
{
  "cmd_id": "unique-command-id",
  "status": "success" | "failed" | "invalid",
  "error": "error message if failed"
}
```

### WiFi/API Protocol

When WiFi is configured, device communicates directly with API:

**Base URL:** `https://api.getkeypr.com/device/v1`

**Authentication:** Device uses its serial number + secret key

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/commands` | Poll for pending commands |
| POST | `/commands/:id/ack` | Acknowledge command |
| POST | `/status` | Report current status |
| POST | `/events` | Report events (tamper, etc.) |

### Status Report Format

```json
{
  "device_id": "serial-number",
  "timestamp": 1704567890,
  "lock_state": "locked" | "unlocked",
  "lid_state": "open" | "closed",
  "battery_percent": 85,
  "wifi_connected": true,
  "firmware_version": "1.0.0"
}
```

### Event Report Format

```json
{
  "device_id": "serial-number",
  "timestamp": 1704567890,
  "event_type": "tamper_detected" | "low_battery" | "emergency_unlock",
  "data": { ... }
}
```

---

## Commands

### LOCK

Lock the device.

```json
{
  "cmd": "lock",
  "id": "cmd-123",
  "ts": 1704567890
}
```

**Behavior:**
1. Activate servo/solenoid to locked position
2. Update internal state to LOCKED
3. Send acknowledgment
4. Notify via BLE characteristic

### UNLOCK

Unlock the device.

```json
{
  "cmd": "unlock",
  "id": "cmd-124",
  "ts": 1704567890
}
```

**Behavior:**
1. Activate servo/solenoid to unlocked position
2. Update internal state to UNLOCKED
3. Send acknowledgment
4. Notify via BLE characteristic

### OPEN_LID

Open the lid (only valid when unlocked).

```json
{
  "cmd": "open_lid",
  "id": "cmd-125",
  "ts": 1704567890
}
```

**Behavior:**
1. Check if device is UNLOCKED (fail if locked)
2. Activate servo/solenoid to release lid
3. Reed switch should detect lid opening
4. Send acknowledgment

### SET_DISPLAY

Show message on e-paper display.

```json
{
  "cmd": "set_display",
  "id": "cmd-126",
  "ts": 1704567890,
  "data": {
    "line1": "Locked",
    "line2": "2d 14h remaining",
    "line3": "Be good! -KH"
  }
}
```

**Behavior:**
1. Clear e-paper display
2. Render provided text
3. Refresh display
4. Send acknowledgment

### CONFIGURE_WIFI

Set WiFi credentials.

```json
{
  "cmd": "configure_wifi",
  "id": "cmd-127",
  "ts": 1704567890,
  "data": {
    "ssid": "HomeNetwork",
    "password": "secretpassword"
  }
}
```

**Behavior:**
1. Store credentials securely
2. Attempt to connect
3. Update WiFi status characteristic
4. Send acknowledgment with result

---

## E-Paper Display

### Display Content

The 1.54" e-paper should show:

```
┌─────────────────────┐
│      🔒 LOCKED      │  ← Lock state icon + text
│                     │
│   2d 14h 32m        │  ← Time remaining (if timed session)
│                     │
│   "Be good! -KH"    │  ← Keyholder message (if set)
└─────────────────────┘
```

### Display Updates

Update display when:
- Lock state changes
- Timer updates (every minute when locked)
- New Keyholder message received
- Low battery warning
- Tamper alert

### Discrete Mode

User can request blank display for discretion. Store flag in settings, clear display when enabled.

---

## Timer Enforcement (Offline)

When device has an active timed session and loses connectivity:

1. Store session end time locally
2. Continue countdown locally
3. When timer expires:
   - If emergency unlock = "immediate": auto-unlock
   - If emergency unlock = "delayed" or "keyholder_required": stay locked, await reconnection
4. When connectivity restored, sync with API for actual state

---

## Tamper Detection

### Detection Methods

1. **Case tamper**: Detect if enclosure is opened
2. **Force detection**: Unusual stress on lock mechanism
3. **Voltage monitoring**: Power line manipulation
4. **Pattern analysis**: Unusual command patterns

### Tamper Response

1. Log tamper event with timestamp
2. Send alert via BLE (Tamper Alert characteristic)
3. If WiFi connected, POST to `/events`
4. Continue normal operation (alert only, no lockdown)

---

## Emergency Unlock (Physical)

User can perform emergency unlock via physical button pattern:

**Pattern:** (Example: 3 long presses, 2 short presses)

**Behavior:**
1. Recognize pattern input
2. Check stored emergency unlock mode:
   - **Immediate**: Unlock now, report to API
   - **Delayed**: Start countdown, report to API
   - **Keyholder Required**: Report to API, await response
3. Display status on e-paper
4. Report emergency event

---

## Power Management

Target: 2+ weeks standby

**Strategies:**
- Deep sleep when idle
- Wake on BLE connection
- E-paper only updates when needed (no continuous refresh)
- WiFi polling interval: every 5 minutes when idle
- Reduce WiFi polling when on battery (every 15 minutes)
- Low battery warning at 20%
- Critical battery warning at 5%

---

## Security Considerations

- Store WiFi credentials encrypted
- Use secure BLE pairing (bonding)
- Validate command signatures if implemented
- Device secret key should be unique per device
- Never expose secret key via BLE

---

## Firmware Updates (Future)

OTA updates via BLE:
1. App downloads firmware from API
2. App sends firmware to device in chunks
3. Device validates checksum
4. Device applies update and reboots

---

## Reference PRDs

For full context, see:
- `prd-master-overview.md` - Complete system overview
- `prd-03-device-management.md` - BLE pairing, device registration
- `prd-04-lock-control.md` - Command flow, emergency unlock
- `prd-08-messaging.md` - Device display messages
