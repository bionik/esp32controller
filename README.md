# esp32controller

WiFi-enabled ESP32S3 game controller with web interface and OTA updates, for Amiga, C64 etc.
ESP hosts a websocket server which can be used to change the state of controller pins.

Uses ESP32S3 supermini controller

Future plans:
* To be able to connect an usb controller to pass controls to the game console.
* Support for bluetooth connectivity (BLE HID controllers, ps5, switch etc).

Functionality:
* Hold the boot button for 5 seconds to pair with a controller in pairing mode. The pairing mode is searching for 20 seconds. 
* Hold boot button for 10 seconds to forget paired controller.
* During boot, if an already paired controller is found, it is connected to again.
* All the connectivity works at the same time, last input overrides the state.
* If a websocket, bluetooth, usb connection is lost, always reset the pins to clear stuck state.

Led color explanations:
* When red led is shown, ESP32 is not connected to wifi
* When green led is shown, user is connected to the websocket
* When purple led is show, usb is connected
* When blue led is shown, bluetooth controller is connected - regardless of the wifi or websocket status
* When in pairing mode, led quickly flashes green on new device, red if none found, blue if existing device is connected. Then returns to default state.
* The priority of the color is in this order.

## Project Files

- **Hardware**: KiCad PCB design files (`esp32controller.kicad_*`) with custom ESP32S3 Supermini footprint
- **Firmware**: PlatformIO project in `firmware/` with WiFi control server and ElegantOTA support
- **Libraries**: Custom symbol (`project_parts.kicad_sym`) and footprint (`project_parts.pretty/`) libraries
