#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Bluepad32.h>

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
#define RGB_BRIGHTNESS 64  // Brightness (0-255)

// Boot button configuration
#define BOOT_BUTTON 0  // GPIO 0 is typically the BOOT button on ESP32
#define PAIRING_HOLD_TIME 3000  // 3 seconds to enter pairing mode
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

    for (int i = 0; i < numControls; i++) {
        if (label == controlPins[i].label) {
            digitalWrite(controlPins[i].pin, state);
            return; 
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
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
    if (DEBUG) Serial.println("Entering pairing mode - forgetting all devices");
    pairingMode = true;
    pairingStartTime = millis();
    lastBlinkTime = 0;
    blinkState = false;
    
    // Forget all paired devices and enable scanning
    BP32.forgetBluetoothKeys();
    BP32.enableNewBluetoothConnections(true);
    
    updateLedState();
}

void stopPairingMode(bool success) {
    if (DEBUG) {
        if (success) {
            Serial.println("Pairing mode ended - device connected");
        } else {
            Serial.println("Pairing mode timeout - no device found");
        }
    }
    pairingMode = false;
    updateLedState();
}

void onConnectedBTController(ControllerPtr ctl) {
    if (DEBUG) Serial.println("Bluetooth Controller connected");
    btConnected = true;
    
    // If we were in pairing mode, exit it successfully
    if (pairingMode) {
        stopPairingMode(true);
    } else {
        updateLedState();
    }
}

void onDisconnectedBTController(ControllerPtr ctl) {
    if (DEBUG) Serial.println("Bluetooth Controller disconnected");
    btConnected = false;
    updateLedState();
    resetPins();
}

void setup() {
    Serial.begin(115200);

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

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    
    // WiFi connected
    wifiConnected = true;
    updateLedState();

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    
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
            if (DEBUG) Serial.println("WiFi reconnected");
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