#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Bluepad32.h>
#include <cstring>

// Try to include credentials from separate file
// If credentials.h doesn't exist, fall back to defaults below
#if __has_include("credentials.h")
    #include "credentials.h"
#else
    #warning "credentials.h not found, using default credentials"
    #define WIFI_SSID "YOUR_WIFI_SSID"
    #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
    #define OTA_USERNAME "admin"
    #define OTA_PASSWORD "secure123"
#endif

// Debug flag - set to 0 to disable debug prints
#define DEBUG 1

// Built-in RGB LED Configuration
#define RGB_BRIGHTNESS 32  // Brightness (0-255)

// Boot button configuration
#define BOOT_BUTTON 0  // GPIO 0 is typically the BOOT button on ESP32
#define PAIRING_HOLD_TIME 2200  // How long press to enter pairing mode
#define PAIRING_TIMEOUT 20000   // 20 seconds pairing timeout
#define PAIRING_BLINK_INTERVAL 100  // Fast blink interval in ms

// Connection state variables
bool wifiConnected = false;
bool wsConnected = false;
bool btConnected = false;

// Pairing mode variables
bool pairingMode = false;
unsigned long pairingStartTime = 0;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// Button state tracking
bool buttonPressed = false;
unsigned long buttonPressStart = 0;

void setLedColor(int r, int g, int b) {
#ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, r, g, b);
#endif
}

void updateLedState() {
    // Pairing mode overrides all other LED states with fast blinking blue
    if (pairingMode) {
        unsigned long currentTime = millis();
        if (currentTime - lastBlinkTime >= PAIRING_BLINK_INTERVAL) {
            blinkState = !blinkState;
            lastBlinkTime = currentTime;
            if (blinkState) {
                setLedColor(0, 0, RGB_BRIGHTNESS);  // Blue
            } else {
                setLedColor(0, 0, 0);  // Off
            }
        }
        return;
    }
    
    // Normal LED state based on connections
    int ledState = (wifiConnected << 2) | (wsConnected << 1) | (btConnected);
    switch (ledState) {
        case 0b000: 
            setLedColor(0, RGB_BRIGHTNESS, 0);   // Green (Idle)
            break;
        case 0b100: 
            setLedColor(RGB_BRIGHTNESS, 0, 0);   // Red (WiFi only)
            break;
        case 0b110: 
            setLedColor(RGB_BRIGHTNESS, RGB_BRIGHTNESS, 0); // Yellow (WiFi + WS)
            break;
        case 0b001: 
            setLedColor(0, 0, RGB_BRIGHTNESS);   // Blue (BT only)
            break;
        case 0b101: 
            setLedColor(RGB_BRIGHTNESS, 0, RGB_BRIGHTNESS); // Magenta (WiFi + BT)
            break;
        case 0b111: 
            setLedColor(RGB_BRIGHTNESS, RGB_BRIGHTNESS, RGB_BRIGHTNESS); // White (All)
            break;
        default:
            setLedColor(0, RGB_BRIGHTNESS, 0);   // Default to green
            break;
    }
}

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* ota_user = OTA_USERNAME;
const char* ota_pass = OTA_PASSWORD; 

void resetPins();
void setControlPin(const char* label, bool state);

void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case SYSTEM_EVENT_STA_GOT_IP:
            wifiConnected = true;
            if (DEBUG) Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
            updateLedState();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            if (wifiConnected) {
                wifiConnected = false;
                if (DEBUG) Serial.println("WiFi disconnected");
                resetPins();
                updateLedState();
            }
            break;
        default:
            break;
    }
}

struct ControlBinding {
    const char* label;
    int pin;
};

ControlBinding controlPins[] = {
    {"up",      11},
    {"down",    13},
    {"left",    10},
    {"right",   1},
    {"button1", 9},
    {"button2", 7},
    {"button3", 3}
};

const int numControls = sizeof(controlPins) / sizeof(controlPins[0]);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Currently connected Bluetooth controller (if any)
ControllerPtr activeController = nullptr;

void resetPins() {
    for (int i = 0; i < numControls; i++) {
        digitalWrite(controlPins[i].pin, LOW);
    }
    if (DEBUG) Serial.println("GPIOs Reset for safety");
}

// --- OTA Callback Functions ---
void onOTAStart() {
    Serial.println("OTA Update started!");
    resetPins(); // Kill all GPIOs before update begins
}

void onOTAEnd(bool success) {
    if (success) {
        Serial.println("OTA Update finished successfully!");
        // Small delay to allow the Serial buffer to flush and the 
        // WebSocket 'Success' message to reach the client.
        delay(1000); 
        
        // Although ElegantOTA does this by default, 
        // force it here if you've disabled auto-reboot.
        ESP.restart(); 
    } else {
        Serial.println("OTA Update failed!");
        // Perhaps reset pins again as a fallback if the update crashed mid-way
        resetPins();
    }
}

void handleCommand(String message) {
    message.trim();
    if (DEBUG) Serial.println("Message received: " + message);
    
    // Check for global state commands
    if (message == "connect" || message == "disconnect") {
        resetPins();
        return;
    }

    int separatorIdx = message.indexOf(':');
    if (separatorIdx == -1) return;

    String label = message.substring(0, separatorIdx);
    String action = message.substring(separatorIdx + 1);
    bool state = (action == "down");

    // Use the centralized control setter
    setControlPin(label.c_str(), state);
}

// Set a control pin by its ascii label ("up", "down", "button1", ...)
void setControlPin(const char* label, bool state) {
    if (DEBUG) Serial.printf("setControlPin called: '%s' -> %s\n", label, state ? "true" : "false");
    for (int i = 0; i < numControls; i++) {
        if (strcmp(label, controlPins[i].label) == 0) {
            digitalWrite(controlPins[i].pin, state ? HIGH : LOW);
            if (DEBUG) Serial.printf("Control '%s' -> %s (GPIO %d)\n", label, state ? "HIGH" : "LOW", controlPins[i].pin);
            return;
        }
    }
    if (DEBUG) Serial.printf("Unknown control '%s'\n", label);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    if (DEBUG) {
        if (client) {
            Serial.printf("WebSocket event %u for client #%u\n", type, client->id());
        } else {
            Serial.printf("WebSocket event %u\n", type);
        }
    }
    switch (type) {
        case WS_EVT_CONNECT:
            if (DEBUG) Serial.printf("WebSocket client #%u connected\n", client->id());
            wsConnected = true;
            updateLedState();
            resetPins(); // Safety reset on new connection
            break;

        case WS_EVT_DISCONNECT:
            if (DEBUG) Serial.printf("WebSocket client #%u disconnected\n", client->id());
            wsConnected = false;
            updateLedState();
            resetPins(); // Safety reset on loss of connection
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                // Create a local buffer to safely null-terminate the incoming data
                char* tempBuffer = (char*)malloc(len + 1);
                if (tempBuffer) {
                    memcpy(tempBuffer, data, len);
                    tempBuffer[len] = '\0';
                    handleCommand(String(tempBuffer));
                    free(tempBuffer);
                }
            }
            break;
        }
            
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void startPairingMode() {
    if (DEBUG) {
        Serial.println("=== Entering pairing mode ===");
        Serial.println("Forgetting all paired devices and enabling scanning...");
    }
    
    pairingMode = true;
    pairingStartTime = millis();
    lastBlinkTime = 0;
    blinkState = false;
    
    // Reset connection flag
    btConnected = false;
    resetPins();
    
    // Forget all paired devices (this also disconnects any connected controllers)
    BP32.forgetBluetoothKeys();
    
    // Enable scanning for new connections
    BP32.enableNewBluetoothConnections(true);
    
    if (DEBUG) Serial.println("Pairing mode active - put your controller in pairing mode now!");
    
    updateLedState();
}

void stopPairingMode(bool success) {
    if (DEBUG) {
        if (success) {
            Serial.println("=== Pairing mode ended - device connected ===");
        } else {
            Serial.println("=== Pairing mode timeout - no device found ===");
            Serial.println("Disabling new Bluetooth connections");
            // Disable further scanning to save power
            BP32.enableNewBluetoothConnections(false);
        }
    }
    pairingMode = false;
    updateLedState();
}

void onConnectedBTController(ControllerPtr ctl) {
    if (DEBUG) {
        Serial.println("=== Bluetooth Controller connected ===");
        Serial.printf("Controller model: %s\n", ctl->getModelName().c_str());
    }
    
    btConnected = true;
    // Track the active controller so we can poll its inputs
    activeController = ctl;
    
    // If we were in pairing mode, exit it successfully and disable further scanning
    if (pairingMode) {
        if (DEBUG) Serial.println("Disabling new Bluetooth connections");
        BP32.enableNewBluetoothConnections(false);
        stopPairingMode(true);
    } else {
        updateLedState();
    }
}

void onDisconnectedBTController(ControllerPtr ctl) {
    if (DEBUG) {
        Serial.println("=== Bluetooth Controller disconnected ===");
    }
    
    btConnected = false;
    updateLedState();
    resetPins();
    
    // Clear active controller reference
    if (activeController == ctl) activeController = nullptr;
}

void setup() {
    Serial.begin(115200);

    if (DEBUG) Serial.println("Booting esp32controller...");

    //BP32.enableNewBluetoothConnections(false);

    // Initialize built-in RGB LED
#ifdef RGB_BUILTIN
    pinMode(RGB_BUILTIN, OUTPUT);
#endif
    updateLedState(); // Show initial state (all disconnected - green)
    
    // Initialize boot button with internal pullup
    pinMode(BOOT_BUTTON, INPUT_PULLUP);

    for (int i = 0; i < numControls; i++) {
        pinMode(controlPins[i].pin, OUTPUT);
    }
    resetPins();
    delay(200); 

    if (DEBUG) Serial.printf("Connecting to WiFi SSID '%s'...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    WiFi.onEvent(onWiFiEvent);

    delay(500); 

    WiFi.begin(ssid, password);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Hello! Connect with websocket to control the device.");
    });

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    if (DEBUG) Serial.println("WebSocket handler attached at /ws");
    
    // Setup ElegantOTA with Password and Hooks
    ElegantOTA.begin(&server, ota_user, ota_pass); 
    ElegantOTA.onStart(onOTAStart);
    ElegantOTA.onEnd(onOTAEnd);

    BP32.setup(&onConnectedBTController, &onDisconnectedBTController);
    
    server.begin();
    if (DEBUG) Serial.println("Ready. IP: " + WiFi.localIP().toString());
}

void loop() {
    BP32.update();
    // If a Bluetooth controller is connected, poll its state and map to controls
    if (activeController && activeController->hasData()) {
        // Use digital D-Pad values
        uint8_t d = activeController->dpad();
        bool up = (d & DPAD_UP);
        bool down = (d & DPAD_DOWN);
        bool left = (d & DPAD_LEFT);
        bool right = (d & DPAD_RIGHT);

        setControlPin("up", up);
        setControlPin("down", down);
        setControlPin("left", left);
        setControlPin("right", right);

        // Map face buttons to button1..3 - y for button3, since it's seldomly used. x for button2 to allow different play styles
        setControlPin("button1", activeController->a());
        setControlPin("button2", activeController->b() || activeController->x());
        setControlPin("button3", activeController->y());
    }
    
    ElegantOTA.loop();
    ws.cleanupClients();
    
    // Handle boot button for pairing mode
    bool currentButtonState = (digitalRead(BOOT_BUTTON) == LOW);  // LOW when pressed (active low)
    
    if (currentButtonState && !buttonPressed) {
        // Button just pressed
        buttonPressed = true;
        buttonPressStart = millis();
    } else if (!currentButtonState && buttonPressed) {
        // Button released
        buttonPressed = false;
    } else if (buttonPressed && !pairingMode) {
        // Button is being held - check if held long enough
        if (millis() - buttonPressStart >= PAIRING_HOLD_TIME) {
            startPairingMode();
            buttonPressed = false;  // Prevent retriggering
        }
    }
    
    // Check pairing mode timeout
    if (pairingMode) {
        if (millis() - pairingStartTime >= PAIRING_TIMEOUT) {
            stopPairingMode(false);  // Timeout
        }
    }
    
    // Monitor WiFi connection state
    bool currentWifiState = (WiFi.status() == WL_CONNECTED);
    if (currentWifiState != wifiConnected) {
        wifiConnected = currentWifiState;
        if (!wifiConnected) {
            if (DEBUG) Serial.println("WiFi disconnected");
            resetPins(); // Safety reset on WiFi loss
        } else {
            if (DEBUG) Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        }
        updateLedState();
    }
    
    // Monitor WebSocket connection state
    bool currentWsState = (ws.count() > 0);
    if (currentWsState != wsConnected) {
        wsConnected = currentWsState;
        if (!wsConnected && DEBUG) {
            Serial.println("All WebSocket clients disconnected");
        }
        updateLedState();
    }
    
    // Update LED state continuously (needed for blinking in pairing mode)
    if (pairingMode) {
        updateLedState();
    }
}