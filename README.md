# Anusmarak

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1.0-red) ![Target](https://img.shields.io/badge/target-ESP32--S3-blue) ![C++|58x20](https://img.shields.io/badge/C%2B%2B-23-green)

**Anusmarak** (Sanskrit: "one who remembers") is a battery-powered, fully autonomous indoor robot built on an ESP32-S3 by Team Engram. On power-up it cruises forward with no external commands, holds its heading with a gyroscope PID loop, slows for obstacles, and turns away from them on its own. A round TFT shows a face with a battery ring, and a Wi-Fi dashboard shows live telemetry and logs.

## Features

- **Autonomous navigation:** 100Hz PID heading hold on MPU-6050 IMU yaw rate; 10Hz PD speed law on HC-SR04 range; at the standoff distance (20cm) it probes left and right and turns toward the clearer side, or turns back if both are blocked.
- **Skid-steer drive:** four DC motors through two L298N modules, 8-bit PWM at 20kHz.
- **Face and battery ring:** blinking eyes on a 240×240 GC9A01 TFT, ringed by a colour-coded battery gauge. No frame buffer; the screen is drawn in 8-row strips.
- **Wi-Fi dashboard:** telemetry, distance chart, log viewer, OTA firmware update, restart and light-sleep shutdown, served at `http://Anusmarak.local`.
- **Flash logging:** two LittleFS log files (`Orchestrator.log`, `Hardware.log`), flushed per line and cleared at each boot.
- **Self-test:** every device is checked at boot; an on-board RGB LED shows the result. If the motors, IMU or ultrasonic sensor fail, the robot refuses to move.
- **Safe OTA:** two firmware slots with app rollback.

## Current Mock Expo Bring-up Status (October 29, 2026)

Resistors, capacitors and the breadboard are available; the main fuse is not. The team lead has not approved the proposed fuse/copper-wiring work. The 18650 cells are exposed without holders and are soldered together; the strip material appears nickel-plated steel but is unverified. The switch terminal is a closed loop. The USD issue was caused by the IMU being disconnected; connecting the IMU restored USD operation.

**Do not run the battery-powered robot or motors unfused, or with improvised aluminum-to-solder-pad joints.** If the required fuse and approved terminations cannot be obtained, use a non-powered/static demo instead.

## Hardware

| Part | Model |
|---|---|
| Microcontroller | ESP32-S3-WROOM-1-N16R8 (16MB flash, 8MB PSRAM) |
| Motor drivers | 2 × L298N, 4 × TT motors on a 4WD chassis |
| IMU | GY-521 (MPU-6050), IMU Z only |
| Distance sensor | HC-SR04 |
| Display | GC9A01 round 240×240 SPI TFT |
| Power | 3 × 18650 (3S, 11.1V nominal), HW-375 3S 20A BMS, USB-C 3S boost charger, MP1584EN buck to 5V |

Motors run directly from the pack. The buck supplies only the ESP32-S3 and the HC-SR04.

### GPIO Map

| Function | GPIO |
|---|---|
| Module 1 (left): IN1–IN4 | 2, 4, 5, 6 |
| Module 1: ENA, ENB | 14, 15 |
| Module 2 (right): IN1–IN4 | 7, 8, 12, 13 |
| Module 2: ENA, ENB | 16, 38 |
| TFT SCL, SDA, RES, DC, CS, BLK | 17, 18, 47, 21, 41, 40 |
| HC-SR04 TRIG, ECHO | 42, 3 |
| MPU-6050 SCL, SDA | 10, 11 |
| Battery sense (ADC1_CH0) | 1 |
| Wake switch | 9 |

The HC-SR04 ECHO line is 5V and **must** go through a 1kΩ/2kΩ divider before GPIO 3. Full pin allocation, wiring, resistor and capacitor values, and the expected multimeter readings are in the design documents.

## Architecture

Six classes plus an orchestrator, built on FreeRTOS:

| Task | Core | Rate | Role |
|---|---|---|---|
| `DriveTask` | 0 | 100Hz | IMU, heading PID, escape manoeuvre, motors; ECHO ISR also on Core 0 |
| `UITask` | 1 | 40Hz | Face and ring |
| `OrchTask` | 1 | 10Hz | Battery filter, expression |
| `LogTask` | 1 | Queue | Prints and writes log lines |
| `httpd` | 1 | Per request | Dashboard and API |

No heap allocation after boot, no locks on the control path, and logging never blocks the control loop.

## Repository Layout

```
Anusmarak/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── Orchestrator.hpp
│   └── Orchestrator.cpp
└── Utilities/
    ├── CMakeLists.txt
    ├── Configuration.hpp     # Every constant, one namespace per owner
    ├── Utilities.hpp         # Logger, Timer, Hardware, Drive, UI, Network
    ├── Utilities.cpp
    └── Status.html           # Dashboard, embedded in the firmware
```

## Build and Flash

Requires [ESP-IDF v6.1.0](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) and a C++23 toolchain.

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Managed components (`joltwallet/littlefs`, `espressif/led_strip`, `espressif/mdns`) are fetched automatically by the IDF Component Manager.

**Before the first build:**

1. Set your hotspot `SSID` and `PASS` in the `Network` namespace of `Utilities/Configuration.hpp`. The robot joins a 2.4GHz WPA2 hotspot as a station. Do not commit real credentials.
2. On a fresh or erased flash, uncomment the one-time `mkdir("/littlefs/Logs")` line in `Orchestrator::Run()`, flash once, then comment it out again.

**Before first power-up:** keep the robot still for about 2 seconds at boot (the IMU bias is sampled then), and never power the ESP32-S3 from the buck 5V and USB-C at the same time.

## Dashboard and API

Once the robot is on your hotspot, open `http://Anusmarak.local`. The page loads its chart library and fonts from public CDNs, so the browser needs internet access.

| Endpoint | Purpose |
|---|---|
| `GET /` | Dashboard |
| `GET /Telemetry` | JSON: battery, temperature, expression, RSSI, exceptions, uptime, distance |
| `GET /Logs/<name>.log` | Log file text |
| `POST /OTA` | Upload a firmware image |
| `GET /Restart`, `GET /Shutdown` | Halt the robot, then restart or light-sleep (wake with the switch on GPIO 9) |

The restart, shutdown and OTA endpoints have **no authentication**. This is deliberate: the robot is meant for a private exhibition hotspot. Do not expose it to an untrusted network.

## Documentation

- **Software Requirements Specification** (ANUSMARAK-SRS-001): requirements, budget and BOM, GPIO allocation, risk register, electrical calculations, and expected bench measurements.
- **Software Design Document** (ANUSMARAK-SDD-001): architecture, module design, interfaces, point-to-point wiring, memory and error handling.

Where the documents and the code differ, the code is correct and the documents are updated to match.

## Status

Hardware is being assembled. Figures such as runtime (about 2.6 hours at an assumed 150mA per motor) are design targets and have not been measured on the robot.

## Team

Team Engram: Dhritiman Modak (PM), Asmit Kumar Jena (POC), Aryan Pattanaik (SPM), Saumil Karn, Aman Gupta, Harsh Dayma.


## Documentation update

The SDD/SRS were updated to reflect the October 29 Mock Expo hardware status, the confirmed IMU-dependent USD bring-up issue, and the missing-fuse/termination safety gate. The SDD §6.4 now contains three regenerated Mermaid wiring diagrams; standalone `.mmd` source files are included alongside the documents.
