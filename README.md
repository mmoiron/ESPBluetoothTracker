# ESP32 Bluetooth Presence Detector

Detects the presence of Bluetooth Classic devices (phones) using ESP32 and publishes status via MQTT. Designed for Home Assistant integration.

## Features

- Bluetooth Classic presence detection (not BLE)
- MQTT integration for Home Assistant
- Dynamic device configuration via MQTT (no reflashing required)
- Configuration persisted in NVS (survives reboots)
- Runtime pairing mode via MQTT
- Supports up to 3 tracked devices

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
| `.../config/add` | Subscribe | `MAC,name` | Add a device (e.g., `A4:75:B9:53:F7:ED,phone_cristina`) |
| `.../config/remove` | Subscribe | `MAC` or `name` | Remove a device by MAC or name |
| `.../config/list` | Subscribe | any | Request current device list |
| `.../config/devices` | Publish | JSON array | Current device configuration (retained) |

**Device list JSON format:**
```json
[{"mac":"A4:75:B9:53:F7:ED","name":"cristina_phone"},{"mac":"9C:83:06:7B:27:17","name":"marco_phone"}]
```

### Pairing

| Topic | Direction | Payload | Description |
|-------|-----------|---------|-------------|
| `.../pairing/set` | Subscribe | `on` / `off` | Enable/disable pairing mode |
| `.../pairing/status` | Publish | `on` / `off` | Current pairing status (retained) |

## Command Reference

Replace `<ESP32_MAC>` with your ESP32's Bluetooth MAC (e.g., `7C9EBDF54A56`).
Replace `<BROKER>`, `<USER>`, `<PASS>` with your MQTT broker details.

### 1. List configured phones
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> \
  -t "home/presence/<ESP32_MAC>/config/list" -m "1"
```
Response appears in topic: `home/presence/<ESP32_MAC>/config/devices`

### 2. Enable pairing mode
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> \
  -t "home/presence/<ESP32_MAC>/pairing/set" -m "on"
```

### 3. Disable pairing mode
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> \
  -t "home/presence/<ESP32_MAC>/pairing/set" -m "off"
```

### 4. Add phone to list
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> \
  -t "home/presence/<ESP32_MAC>/config/add" -m "AA:BB:CC:DD:EE:FF,phone_name"
```

### 5. Remove phone from list
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> \
  -t "home/presence/<ESP32_MAC>/config/remove" -m "phone_name"
```

### 6. Trigger presence scan
```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> \
  -t "home/presence/<ESP32_MAC>/scan/request" -m "1"
```

## Complete Workflow: Add a New Phone

### Step 1: Enable pairing mode
```bash
mosquitto_pub -h 192.168.1.92 -u mqttuser -P mqttpassword \
  -t "home/presence/7C9EBDF54A56/pairing/set" -m "on"
```

### Step 2: Pair the phone
On the phone: **Settings** → **Bluetooth** → Find **"ESP32_Presence"** → Tap to pair

### Step 3: Note the MAC address
Check the ESP32 serial output for the phone's Bluetooth MAC address.

### Step 4: Disable pairing mode
```bash
mosquitto_pub -h 192.168.1.92 -u mqttuser -P mqttpassword \
  -t "home/presence/7C9EBDF54A56/pairing/set" -m "off"
```

### Step 5: Add the phone to tracking list
```bash
mosquitto_pub -h 192.168.1.92 -u mqttuser -P mqttpassword \
  -t "home/presence/7C9EBDF54A56/config/add" -m "9C:83:06:7B:27:17,marco_phone"
```

### Step 6: Verify configuration
```bash
mosquitto_pub -h 192.168.1.92 -u mqttuser -P mqttpassword \
  -t "home/presence/7C9EBDF54A56/config/list" -m "1"
```

### Step 7: Test presence detection
```bash
mosquitto_pub -h 192.168.1.92 -u mqttuser -P mqttpassword \
  -t "home/presence/7C9EBDF54A56/scan/request" -m "1"
```

## Complete Workflow: Remove a Phone

### Step 1: Remove from tracking list
```bash
mosquitto_pub -h 192.168.1.92 -u mqttuser -P mqttpassword \
  -t "home/presence/7C9EBDF54A56/config/remove" -m "marco_phone"
```

### Step 2: Verify removal
```bash
mosquitto_pub -h 192.168.1.92 -u mqttuser -P mqttpassword \
  -t "home/presence/7C9EBDF54A56/config/list" -m "1"
```

### Step 3: (Optional) Unpair from phone
On the phone: **Settings** → **Bluetooth** → **ESP32_Presence** → **Forget device**

## Home Assistant Integration

### Binary Sensor Configuration

```yaml
mqtt:
  binary_sensor:
    - name: "Cristina Phone"
      state_topic: "home/presence/AABBCCDDEEFF/cristina_phone/state"
      payload_on: "home"
      payload_off: "not_home"
      device_class: presence

    - name: "Marco Phone"
      state_topic: "home/presence/AABBCCDDEEFF/marco_phone/state"
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
          topic: "home/presence/AABBCCDDEEFF/scan/request"
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
