# ESP32 Bluetooth Presence Detector

Detects the presence of Bluetooth Classic devices (phones) using ESP32 and publishes status via MQTT. Designed for Home Assistant integration.

## Features

- Bluetooth Classic presence detection (not BLE)
- MQTT integration for Home Assistant
- **WiFi Provisioning via Captive Portal** - no hardcoded credentials required
- Dynamic device configuration via MQTT (no reflashing required)
- Configuration persisted in NVS (survives reboots)
- Runtime pairing mode via MQTT
- Unique Bluetooth name per device (e.g., `ESP32_Presence_4A56`)
- Supports up to 3 tracked devices
- Support for multiple ESP32 devices in the same network
- Factory reset via BOOT button (hold 5 seconds)

## Security Notice

**This is a personal/hobby project designed for use on a trusted home network. No security hardening has been implemented.**

Known security limitations:

- **No TLS/SSL support** - MQTT connections use plain text (`mqtt://`), not encrypted (`mqtts://`)
- **Provisioning portal uses HTTP** - WiFi and MQTT credentials are transmitted unencrypted during setup
- **Open WiFi AP during provisioning** - The setup access point has no password protection
- **Credentials stored unencrypted** - WiFi/MQTT passwords are stored in NVS without encryption
- **No MQTT authentication enforcement** - Any device on the network can publish commands to control the ESP32
- **No input validation** - MQTT payloads are not sanitized (potential buffer overflow risks)
- **Legacy Bluetooth pairing** - Uses simple pairing without Secure Simple Pairing (SSP)
- **MAC addresses exposed** - Device MAC addresses are visible in MQTT topics

**Recommendations if deploying in a less trusted environment:**
- Use a dedicated IoT VLAN with firewall rules
- Configure MQTT broker ACLs to restrict topic access
- Use MQTT broker authentication
- Consider this device as untrusted and isolate accordingly

**USE AT YOUR OWN RISK. The author accepts no responsibility for security incidents arising from the use of this code.**

## MQTT Topics

All topics use the prefix: `home/presence/<ESP32_MAC>/`

Where `<ESP32_MAC>` is the ESP32's Bluetooth MAC address without colons (e.g., `AABBCCDDEEFF`).

### Scanning

| Topic | Direction | Payload | Description |
|-------|-----------|---------|-------------|
| `.../scan/request` | Subscribe | any | Trigger a presence scan |
| `.../scan/status` | Publish | `started` / `completed` | Scan status updates |
| `.../<device_name>/state` | Publish | `home` / `not_home` | Presence state (retained) |

### Configuration

| Topic | Direction | Payload | Description |
|-------|-----------|---------|-------------|
| `.../config/add` | Subscribe | `MAC,name` | Add a device (e.g., `AA:BB:CC:DD:EE:FF,my_phone`) |
| `.../config/remove` | Subscribe | `MAC` or `name` | Remove a device by MAC or name |
| `.../config/list` | Subscribe | any | Request current device list |
| `.../config/devices` | Publish | JSON array | Current device configuration (retained) |

**Device list JSON format:**
```json
[{"mac":"AA:BB:CC:DD:EE:FF","name":"phone_1"},{"mac":"11:22:33:44:55:66","name":"phone_2"}]
```

### Pairing

| Topic | Direction | Payload | Description |
|-------|-----------|---------|-------------|
| `.../pairing/set` | Subscribe | `on` / `off` | Enable/disable pairing mode |
| `.../pairing/status` | Publish | `on` / `off` | Current pairing status (retained) |

### System

| Topic | Direction | Payload | Description |
|-------|-----------|---------|-------------|
| `.../system/reprovision` | Subscribe | any | Clear WiFi/MQTT config and restart into provisioning mode |
| `.../system/factory_reset` | Subscribe | any | Clear ALL config (WiFi, MQTT, devices) and restart |

### Factory Reset (Physical Button)

**Hold the BOOT button for 5 seconds** to perform a complete factory reset. This clears:
- WiFi credentials
- MQTT broker settings
- All tracked phone devices

The device will restart in provisioning mode (captive portal).

## Initial Setup (Provisioning)

When the ESP32 is flashed without WiFi configuration (or after a `reprovision` command), it automatically enters **Provisioning Mode**:

1. **ESP32 creates a WiFi Access Point** named `ESP32_Presence_Setup`
2. **Connect your phone/laptop** to this WiFi network
3. **Open a browser** and go to `http://192.168.4.1`
4. **Enter your settings**:
   - WiFi network name (SSID) and password
   - MQTT broker URI (e.g., `mqtt://192.168.1.100:1883`)
   - MQTT username and password
5. **Click Save** - the device will restart and connect to your network

### Re-provisioning (Changing WiFi/MQTT Settings)

To reconfigure an already-provisioned device:

```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/system/reprovision" -m "1"
```

The device will clear its configuration and restart in provisioning mode.

## Command Reference

Replace `<ESP32_MAC>` with your ESP32's Bluetooth MAC (e.g., `AABBCCDDEEFF`).
Replace `<BROKER>`, `<USER>`, `<PASS>` with your MQTT broker details.

### 1. List configured phones
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/config/list" -m "1"
```
Response appears in topic: `home/presence/<ESP32_MAC>/config/devices`

### 2. Enable pairing mode
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/pairing/set" -m "on"
```

### 3. Disable pairing mode
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/pairing/set" -m "off"
```

### 4. Add phone to list
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/config/add" -m "AA:BB:CC:DD:EE:FF,phone_name"
```

### 5. Remove phone from list
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/config/remove" -m "phone_name"
```

### 6. Trigger presence scan
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/scan/request" -m "1"
```

### 7. Factory reset (clear everything)
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/system/factory_reset" -m "1"
```

## Complete Workflow: Add a New Phone

Replace `<BROKER>`, `<USER>`, `<PASS>` with your MQTT broker details.
Replace `<ESP32_MAC>` with your ESP32's Bluetooth MAC (e.g., `AABBCCDDEEFF`).

### Step 1: Enable pairing mode
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/pairing/set" -m "on"
```

### Step 2: Pair the phone
On the phone: **Settings** → **Bluetooth** → Find **"ESP32_Presence_XXXX"** (where XXXX is the last 4 chars of MAC) → Tap to pair

### Step 3: Note the MAC address
Check the ESP32 serial output for the phone's Bluetooth MAC address.

### Step 4: Disable pairing mode
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/pairing/set" -m "off"
```

### Step 5: Add the phone to tracking list
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/config/add" -m "AA:BB:CC:DD:EE:FF,my_phone"
```

### Step 6: Verify configuration
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/config/list" -m "1"
```

### Step 7: Test presence detection
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/scan/request" -m "1"
```

## Complete Workflow: Remove a Phone

### Step 1: Remove from tracking list
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/config/remove" -m "my_phone"
```

### Step 2: Verify removal
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/presence/<ESP32_MAC>/config/list" -m "1"
```

### Step 3: (Optional) Unpair from phone
On the phone: **Settings** → **Bluetooth** → **ESP32_Presence_XXXX** → **Forget device**

## Home Assistant Integration

### Binary Sensor Configuration

Replace `<ESP32_MAC>` with your ESP32's Bluetooth MAC (e.g., `AABBCCDDEEFF`).

```yaml
mqtt:
  binary_sensor:
    - name: "Phone 1"
      state_topic: "home/presence/<ESP32_MAC>/phone_1/state"
      payload_on: "home"
      payload_off: "not_home"
      device_class: presence

    - name: "Phone 2"
      state_topic: "home/presence/<ESP32_MAC>/phone_2/state"
      payload_on: "home"
      payload_off: "not_home"
      device_class: presence
```

### Automation to Trigger Scans

```yaml
automation:
  - alias: "Scan Bluetooth Presence Every 5 Minutes"
    trigger:
      - platform: time_pattern
        minutes: "/5"
    action:
      - service: mqtt.publish
        data:
          topic: "home/presence/<ESP32_MAC>/scan/request"
          payload: "1"
```

## Building and Flashing

### Prerequisites
- ESP-IDF v5.5.2
- ESP32 development board

### Build
```batch
rebuild.bat
```

### Flash
```batch
flash_project.bat
```

### Monitor Serial Output
```batch
idf.py -p COM3 monitor
```

**Serial settings** (for PuTTY or other terminals):
- Baud rate: 115200
- Data bits: 8
- Parity: None
- Stop bits: 1
- Flow control: None

To exit ESP-IDF monitor: `Ctrl+]`

## Configuration

Default configuration is set via `menuconfig`. Runtime changes via MQTT are persisted in NVS.

### Kconfig Options
- WiFi SSID and password
- MQTT broker URI, username, password
- Default device MAC addresses and names
- Probe timeout and scan intervals

## Technical Details

- Uses Bluetooth Name Request for presence detection (more reliable than SDP with phones in standby)
- Probe timeout: 8 seconds per device
- Minimum scan interval: 2 seconds between scans
- Maximum 3 tracked devices
- Unique Bluetooth device name based on MAC address (last 4 characters)
- Configuration persisted in NVS (Non-Volatile Storage)
