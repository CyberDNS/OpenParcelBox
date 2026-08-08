# OpenParcelBox2

An ESP32-S3 smart parcel box with a touchscreen UI, PIN and BLE unlock, MQTT/Home Assistant integration, and a servo-controlled latch — built for a shared residential building.

## Hardware

- **[Waveshare ESP32-S3-Touch-LCD-3.5](https://www.waveshare.com/esp32-s3-touch-lcd-3.5.htm)** — ESP32-S3, 3.5" ST7796 touchscreen, FT6336 touch controller, AXP2101 power management
- **Servo** — controls the latch; two positions: parcel-only compartment and full open (parcel + letter)

## Features

- **PIN entry** on the touchscreen — up to 16 PIN codes, each scoped to parcel-only or full open
- **BLE unlock** — allowlist of up to 8 device MAC addresses; no PIN required
- **Home Assistant integration** — MQTT command topic triggers open; MQTT Discovery registers two entities automatically (access event + last-opened sensor)
- **Access logging** — access, denied, and error logs stored on-device (LittleFS) and mirrored in full to MQTT retained topics
- **Encrypted credential storage** — WiFi and MQTT passwords stored on-device using AES-256-CBC with a key derived from the chip's unique eFuse MAC
- **Hidden admin entry** — long-press the firmware version label (bottom-right, 3 s) to reach the admin PIN screen and settings

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- A Waveshare ESP32-S3-Touch-LCD-3.5

### Build and flash

```bash
# Clone
git clone https://github.com/CyberDNS/OpenParcelBox2.git
cd OpenParcelBox2

# Copy and edit the per-installation config
cp include/building_config.h.example include/building_config.h
# Edit include/building_config.h to set BUILDING_RESIDENTS

# Build and flash
pio run --target upload

# Upload the LittleFS filesystem image (required on first flash)
pio run --target uploadfs
```

### First-time setup

After flashing, the box has no WiFi or MQTT credentials yet:

1. Long-press the firmware version label in the bottom-right corner for 3 seconds.
2. Enter the admin PIN (default: see your initial config push, or set one via MQTT).
3. Fill in WiFi SSID/password and MQTT host/username/password, then tap **Save & Reboot**.

The device connects to WiFi and MQTT on the next boot.

## Configuration

Access control (PINs, BLE allowlist, unlock duration) is delivered via MQTT and persisted locally — the device does not need re-flashing to add or change codes.

Publish a JSON payload to `openparcelbox/config` (retained):

```json
{
  "version": 1,
  "unlock_duration_ms": 5000,
  "pins": [
    { "id": "Courier DHL",  "pin": "1234", "scope": "parcel" },
    { "id": "Resident One", "pin": "5678", "scope": "full"   }
  ],
  "ble_allowed": [
    "AA:BB:CC:DD:EE:FF"
  ]
}
```

| Field | Description |
|---|---|
| `version` | Increment to force the device to reload config |
| `unlock_duration_ms` | How long the latch stays open |
| `pins[].scope` | `"parcel"` (parcel compartment only) or `"full"` (parcel + letter) |
| `ble_allowed` | MAC addresses (colon-separated, uppercase) that unlock without a PIN |

## MQTT Topics

| Topic | Direction | Description |
|---|---|---|
| `openparcelbox/config` | → device | Access control config (see above), retained |
| `openparcelbox/command/open` | → device | `"parcel"` or `"full"` — opens without PIN check |
| `openparcelbox/access/event` | device → | Fires on every successful open (HA Event entity) |
| `openparcelbox/access/last` | device → | Last opened-by string, retained (HA Sensor entity) |
| `openparcelbox/log/access` | device → | Full access log, retained |
| `openparcelbox/log/denied` | device → | Full denied-access log, retained |
| `openparcelbox/log/error` | device → | Full error log, retained |

Home Assistant MQTT Discovery is published automatically on connect — no manual HA configuration needed.

## Servo Calibration

Edit `include/servo_config.h` to match your physical mechanism, then reflash:

```cpp
#define SERVO_ANGLE_PARCEL_ONLY 70   // degrees — parcel compartment only
#define SERVO_ANGLE_FULL        110  // degrees — parcel + letter compartment
#define SERVO_SWEEP_MS          1000 // total ease-in/out travel time
```

## Per-Installation Files

| File | In git | Purpose |
|---|---|---|
| `include/building_config.h` | No | Residents shown on the home screen |
| `include/building_config.h.example` | Yes | Template — copy to `building_config.h` |
| `include/version.h` | Yes | Firmware version string |

## License

MIT
