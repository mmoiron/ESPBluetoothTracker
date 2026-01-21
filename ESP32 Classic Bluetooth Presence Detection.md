# ESP32 Classic Bluetooth Presence Detection  
## Development, Flashing & Test Guide (Windows VM)

This document contains **all required information** for an AI developer agent to **develop, build, flash, and test** an ESP32 application that performs **presence detection using Bluetooth Classic (SDP probe) triggered via MQTT**, on a **Windows virtual machine**.

Target hardware: **Classic ESP32 (Xtensa)**  
Bluetooth mode: **Classic (BR/EDR), no BLE**  
Devices: **2 bonded mobile phones**  
Output: `present | absent` via MQTT  

---

## 1. Host Environment (Windows VM)

### Supported Windows Versions
- Windows 10 (64-bit)
- Windows 11 (64-bit)

> 32-bit Windows is **not supported** by ESP-IDF.

---

## 2. Required Software (Host)

### 2.1 ESP-IDF (MANDATORY)

**Version**
- Recommended: **ESP-IDF v4.4.x**
- Alternative: **ESP-IDF v5.1.x**

Reason:
- Stable Classic Bluetooth (Bluedroid)
- Mature GAP + SDP support
- Predictable coexistence behavior

**Installation Method (Windows)**
- Use **Espressif ESP-IDF Tools Installer**
- Install:
  - ESP-IDF
  - Xtensa toolchain
  - Python (managed by ESP-IDF)
  - CMake + Ninja
  - esptool.py

No manual dependency installation required.

---

### 2.2 USB-to-Serial Driver (MANDATORY)

Depends on ESP32 board:

| Chip on Board | Driver Needed       |
| ------------- | ------------------- |
| CP2102/CP210x | Silicon Labs CP210x |
| CH340         | WCH CH340           |

Without this:
- COM port will not appear
- Flashing will fail

Verify in **Device Manager → Ports (COM & LPT)**.

---

### 2.3 Code Editor (RECOMMENDED)

**Visual Studio Code**
- Install **Espressif IDF Extension**
  - Project creation
  - Build / Flash / Monitor buttons
  - `menuconfig` GUI
  - Serial monitor

CLI-only development is possible but not recommended.

---

### 2.4 MQTT Broker (MANDATORY FOR TESTING)

One broker is required to:
- trigger scans
- receive presence results

Recommended:
- **Mosquitto (Windows build)**

Install:
- Broker service
- `mosquitto_pub`
- `mosquitto_sub`

Broker can be:
- Local (same VM)
- Remote (LAN server, NAS, etc.)

---

## 3. Hardware Requirements

### 3.1 ESP32 Board
- Classic ESP32 (ESP32-D0WD / ESP32-WROOM / ESP32-WROVER)
- USB cable (data-capable)

**Do NOT use:**
- ESP32-C3
- ESP32-S3
(Classic Bluetooth not supported)

---

### 3.2 Mobile Phones
- 2 phones
- Bluetooth Classic enabled
- Must be **paired/bonded with the ESP32**
- Phone pairing with Raspberry Pi does **not** transfer

---

## 4. ESP-IDF Project Configuration

### 4.1 Bluetooth Configuration (`menuconfig`)

Component config →
Bluetooth →
[] Bluetooth
Bluetooth Host →
[] Bluedroid
[*] Classic Bluetooth
[ ] BLE


### 4.2 Wi-Fi + Coexistence

Component config →
Wi-Fi →
[] WiFi
[] WiFi/Bluetooth coexistence


### 4.3 System
- Default FreeRTOS settings are sufficient
- No PSRAM required

---

## 5. Application Architecture (High-Level)

### Functional Flow
1. ESP32 connects to Wi-Fi
2. ESP32 connects to MQTT broker
3. MQTT message triggers presence scan
4. ESP32 probes 2 bonded devices via **SDP**
5. Result published via MQTT

### Presence Probe
- Classic Bluetooth SDP query:
  - `esp_bt_gap_get_remote_services(bda)`
- Success ⇒ `present`
- Failure or timeout ⇒ `absent`

No Bluetooth inquiry scan is used.

---

## 6. MQTT Topics

### Input (Trigger)
- `home/presence/scan/request`
- Any payload triggers scan

### Output (Per Device)
- `home/presence/<device_id>/state`
```json
{
  "state": "present",
  "ts": 1730000000,
  "reason": "sdp_ok"
}
Optional Lifecycle
home/presence/scan/status

7. Pairing Procedure (One-Time)
Flash firmware with pairing enabled

ESP32 set to:

discoverable

connectable

Pair each phone via phone Bluetooth UI

Verify bond stored

Disable discoverable mode for normal operation

Pairing must occur once per ESP32.

8. Build, Flash & Monitor
Using VS Code
Open project

Select ESP-IDF target: esp32

Build

Flash

Monitor

Using CLI (alternative)
idf.py build
idf.py flash
idf.py monitor
9. Test Procedure
9.1 Trigger Scan
mosquitto_pub -t home/presence/scan/request -m "{}"
9.2 Observe Results
mosquitto_sub -t "home/presence/#"
Expected Behavior
One scan cycle

Two sequential probes

One MQTT result per phone

10. Timing Parameters (Recommended Defaults)
Parameter	Value
Probe timeout	8000 ms
Inter-device delay	300 ms
Min scan interval	2000 ms
Devices	2
11. Known Limitations / Caveats
Phones may refuse Classic connections when:

Bluetooth is ON but phone is sleeping

OS power-saving is aggressive

iOS is more restrictive than Android

Absence should be determined via timeouts + decay, not single failure

12. What Not to Use
Arduino framework

BLE-based presence logic

Inquiry discovery as primary signal

Parallel Classic BT operations

13. Success Criteria
The system is considered working when:

ESP32 reliably publishes present when phone is nearby

ESP32 publishes absent when phone Bluetooth is off or far away

No crashes or BT stack deadlocks after repeated scans