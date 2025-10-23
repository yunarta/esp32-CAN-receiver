# CAN-Agent (ESP32-C3 TWAI/CAN Echo Node)

A minimal ESP32-C3 firmware that joins a CAN (TWAI) bus, acknowledges every valid frame, prints received frames to Serial, and sends a concise echo reply for visibility and link testing.

- MCU/Board: LOLIN C3 Mini (ESP32-C3)
- Framework: Arduino (PlatformIO)
- CAN controller: On-chip TWAI (ESP32-C3)
- Transceiver: SN65HVD230 (or compatible)

## What it does
- Configures the ESP32-C3 TWAI (CAN) in NORMAL mode.
- Accepts all incoming frames and prints them to the Serial monitor.
- For data frames (non-RTR), transmits an echo response with an "ECHO" prefix and up to 4 bytes from the received payload.
- Tracks and prints health counters periodically (RX/TX/ACK/bus errors) and many TWAI alerts.
- Automatically attempts recovery if the bus enters BUS_OFF.

See `src/main.cpp` for the full behavior.

## Hardware and wiring
- MCU: LOLIN C3 Mini (ESP32-C3)
- Transceiver: SN65HVD230 (3.3V-selectable module recommended)
- Pins (default in code):
  - ESP32-C3 GPIO4 (TWAI TX) → Transceiver CTX (DIN/TXD)
  - ESP32-C3 GPIO5 (TWAI RX) ← Transceiver CRX (RO/RXD)
  - 3V3 ↔ VCC, GND ↔ GND
  - CANH/CANL ↔ CAN bus
- Termination: Ensure your CAN bus has 120 Ω termination at both ends (only at ends of the network). Many dev boards or transceiver modules have a jumper for the 120 Ω resistor.
- Common ground: The ESP32-C3 and any other CAN nodes must share ground.

If you use a different ESP32-C3 board or a different transceiver, adjust wiring and the GPIO definitions in code.

## Build and flash (PlatformIO)
This is a standard PlatformIO project. The provided environment is `lolin_c3_mini`.

platformio.ini highlights:

```
[env:lolin_c3_mini]
platform = espressif32
board = lolin_c3_mini
framework = arduino
upload_speed = 921600
upload_port = COM15
monitor_speed = 115200
monitor_port = COM15
```

Steps:
1) Install PlatformIO (VS Code or JetBrains CLion plugin) or the `pio` CLI.
2) Connect the LOLIN C3 Mini to your PC. Identify its COM port.
3) Edit `platformio.ini` to set `upload_port` and `monitor_port` to the correct COM port.
4) Build and upload:
   - VS Code/CLion: Use the PlatformIO "Build" and "Upload" actions for the `lolin_c3_mini` environment.
   - CLI examples:
     - `pio run -e lolin_c3_mini`
     - `pio run -e lolin_c3_mini -t upload`
5) Open a serial monitor at 115200 baud:
   - `pio device monitor -b 115200 -p COM15` (adjust COM port)

On boot you should see something like:

```
TWAI RECEIVER / ECHO (NORMAL) — ACK + reply
Wiring: TX=GPIO4->CTX, RX=GPIO5<-CRX, Bitrate=250 kbps (match master)
[TWAI] started (NORMAL)
[STATUS] start: state=RUNNING ...
```

## Configuration knobs
Edit the top of `src/main.cpp` to tailor behavior:

- Pins:
  - `static int TWAI_TX_GPIO = 4;`
  - `static int TWAI_RX_GPIO = 5;`
- Bitrate:
  - `static twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_250KBITS();`
  - Switch to `TWAI_TIMING_CONFIG_500KBITS()` to match your bus, or other supported presets.
- Echo response ID:
  - `ECHO_RESP_ID_SAME = 1` → reply uses the same ID as the received frame.
  - Set `ECHO_RESP_ID_SAME = 0` and choose `FIXED_RESP_ID` to force a fixed reply ID.
- Alerts: `ALERTS` bitmask controls which TWAI events are reported.

## Runtime behavior
- Prints each received frame, e.g.:

```
[RX] id=0x123 (STD)       dlc=8  11 22 33 44 55 66 77 88
[TX echo] id=0x123 (STD)  dlc=8  45 43 48 4F 11 22 33 44
```

- Periodic health every 5 seconds:

```
[HEALTH] rx=42 tx=42 ack=42 txFail=0 busErr=0 busOff=0
[STATUS] periodic: state=RUNNING ...
```

- Alerts and automatic recovery are printed when relevant, e.g. `BUS_OFF` → recovery sequence until `BUS_RECOVERED`.

## Project layout
- `src/main.cpp` — TWAI receiver/echo logic.
- `include/` — headers (if any).
- `lib/` — external or project libraries (if any).
- `platformio.ini` — PlatformIO environment and upload/monitor settings.

## License
This project is released under the MIT License. See [LICENSE.md](LICENSE.md).

## Troubleshooting
- No output in serial monitor:
  - Check `monitor_port` and `monitor_speed`.
  - Ensure the correct COM port is used and drivers are installed.
- No CAN traffic observed:
  - Verify wiring (TX/RX pins), common ground, and bus termination.
  - Match the bus bitrate (250 kbps vs 500 kbps, etc.).
  - Ensure at least one other active node is present on the CAN bus.
- Repeated BUS_OFF:
  - Usually indicates wiring/termination issues or mismatched bitrate.
