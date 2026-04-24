#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

// --- Configuration ---
#define DEBUG 1

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

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* ota_user = OTA_USERNAME;
const char* ota_pass = OTA_PASSWORD; 

struct ControlBinding {
    const char* label;
    int pin;
};

ControlBinding controls[] = {
    {"up",      11},
    {"down",    13},
    {"left",    10},
    {"right",   1},
    {"button1", 9},
    {"button2", 7},
    {"button3", 3}
};

const int numControls = sizeof(controls) / sizeof(controls[0]);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void resetPins() {
    for (int i = 0; i < numControls; i++) {
        digitalWrite(controls[i].pin, LOW);
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
        if (label == controls[i].label) {
            digitalWrite(controls[i].pin, state);
            return; 
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            if (DEBUG) Serial.printf("WebSocket client #%u connected\n", client->id());
            resetPins(); // Safety reset on new connection
            break;

        case WS_EVT_DISCONNECT:
            if (DEBUG) Serial.printf("WebSocket client #%u disconnected\n", client->id());
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

void setup() {
    Serial.begin(115200);

    for (int i = 0; i < numControls; i++) {
        pinMode(controls[i].pin, OUTPUT);
    }
    resetPins();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    
    // Setup ElegantOTA with Password and Hooks
    ElegantOTA.begin(&server, ota_user, ota_pass); 
    ElegantOTA.onStart(onOTAStart);
    ElegantOTA.onEnd(onOTAEnd);
    
    server.begin();
    if (DEBUG) Serial.println("Ready. IP: " + WiFi.localIP().toString());
}

void loop() {
    ElegantOTA.loop();
    ws.cleanupClients();
}