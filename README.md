# esp32controller

WiFi-enabled ESP32S3 game controller with web interface and OTA updates, for Amiga. C64 could be possible with external power and filtering.
ESP hosts a websocket server which can be used to change the state of controller pins. Bluetooth controllers are also supported.

Uses ESP32S3 supermini controller

Future plans:
* To be able to connect an usb controller to pass controls to the game console. This might not be a good idea for "powered" controllers (rumble, battery charging etc)

Functionality:
* Reboot the device to connect to a bluetooth controller in pairing mode.
* If a websocket or bluetooth is lost, always reset the pins to clear stuck state.

Led color explanations:
* When green led is shown, ESP32 is idle and not connected to wifi or bluetooth.
* When red led is show, wifi is connected but websocket and bluetooth are not connected.
* When yellow led is shown, wifi is connected and user is connected to the websocket.
* When blue led is shown, bluetooth controller is connected.
* When magenta/purple led is shown, wifi is connected but websocket is not connected, bluetooth is connected.
* When white led is shown, wifi, websocket and bluetooth are connected.

## Project Files

- **Hardware**: KiCad PCB design files (`esp32controller.kicad_*`) with custom ESP32S3 Supermini footprint
- **Firmware**: PlatformIO project in `firmware/` with WiFi control server and ElegantOTA support
- **Libraries**: Custom symbol (`project_parts.kicad_sym`) and footprint (`project_parts.pretty/`) libraries
