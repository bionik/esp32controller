# esp32controller

WiFi-enabled ESP32S3 game controller interface with WebSocket control, Bluetooth gamepad support, and OTA firmware updates.

The ESP32S3 runs a small HTTP/WebSocket server for remote control of GPIO pins, and also supports Bluetooth controllers through Bluepad32. The design is aimed at controlling classic systems such as the Amiga, with the option to adapt to other consoles if properly powered and filtered.

Features:
* WiFi connection using credentials from `firmware/src/credentials.h` or fallback defaults.
* WebSocket server at `/ws` for remote button press/release commands.
* ElegantOTA support with authenticated OTA updates.
* Bluetooth controller support using Bluepad32.
* Boot-button pairing mode: long-press the BOOT button to enter Bluetooth pairing.
* Only the last paired Bluetooth controller is accepted after pairing mode, all others are rejected.
* GPIO safety fallback: all control pins are reset when WiFi, WebSocket, or Bluetooth disconnects.
* Phone keypad mapping on websocket: numeric keys `2/4/6/8` map to directions and `1/3/5` map to buttons.
* Built-in RGB LED status reporting for connection states and pairing.

LED color guide:
* Green: idle / no WiFi and no Bluetooth connected.
* Red: WiFi connected but no WebSocket client and no Bluetooth controller.
* Yellow: WiFi connected and WebSocket client connected.
* Blue: Bluetooth controller connected.
* Magenta: WiFi connected and Bluetooth connected, but no WebSocket client.
* White: WiFi, WebSocket, and Bluetooth all connected.
* Fast blinking blue: Bluetooth pairing mode active.

Firmware notes:
* Control pins are defined in `firmware/src/main.cpp` and are driven LOW by default for safety.
* WiFi tries to reconnect automatically after a delay.
* Bluetooth pairing mode times out after 20 seconds if no device is found.
* On OTA start, GPIOs are reset and the device restarts after a successful update.

## Project Files

- **Hardware**: KiCad PCB design files (`esp32controller.kicad_*`) with custom ESP32S3 Supermini footprint
- **Firmware**: PlatformIO project in `firmware/` with WiFi server, WebSocket control, Bluepad32, and ElegantOTA support
- **Libraries**: Custom symbol (`project_parts.kicad_sym`) and footprint (`project_parts.pretty/`) libraries
