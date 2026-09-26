#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>


// ===== Configuration =====
const char* MANIFEST_URL = 
    "https://github.com/ridwan-upc/Build-ESP32-Firmware/releases/latest/download/manifest.json";

const char* CURRENT_VERSION = "1.0.0";

#define LED_PIN 2               // ESP32 built-in LED
#define LED_INTERVAL_MS 500     // Blink every 500ms
#define SERIAL_INTERVAL_MS 1000 // Send serial every 1 second
#define OTA_INTERVAL_MS 3600000 // Check OTA every 1 hour

// ===== State =====
String latestVersion;
String firmwareUrl;
String expectedSha256;

// ===== Function: Connect WiFi via WiFiManager =====
void connectWiFi() {
    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);
    
    Serial.println("Starting WiFiManager...");
    Serial.println("If not configured yet, connect to AP 'ESP32-Setup'");
    Serial.println("Then open http://192.168.4.1 in browser");
    
    bool connected = wifiManager.autoConnect("ESP32-Setup");
    
    if (connected) {
        Serial.println("✅ WiFi connected");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("❌ WiFi setup timeout — rebooting...");
        ESP.restart();
    }
}

// ===== Function: Fetch Manifest =====
bool fetchManifest() {
    HTTPClient http;
    http.begin(MANIFEST_URL);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    Serial.println("[OTA] Fetching manifest...");
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] ❌ HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        Serial.printf("[OTA] ❌ JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    latestVersion = doc["version"].as<String>();
    firmwareUrl = doc["url"].as<String>();
    expectedSha256 = doc["sha256"].as<String>();
    
    Serial.printf("[OTA] Latest: %s | Current: %s\n", 
        latestVersion.c_str(), CURRENT_VERSION);
    
    return true;
}

// ===== Function: Check Version =====
bool isUpdateAvailable() {
    return latestVersion != CURRENT_VERSION;
}

// ===== Function: OTA Update =====
void performOTA() {
    Serial.println("[OTA] Starting OTA update...");
    Serial.printf("[OTA] URL: %s\n", firmwareUrl.c_str());
    
    // HTTPS client
    WiFiClientSecure client;
    client.setInsecure();  // TODO: ganti dengan setCACert() untuk production
    client.setTimeout(30);
    
    // HTTPUpdate dengan redirect support
    HTTPUpdate httpUpdate;
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(false);
    
    // Progress callback (opsional, untuk debug)
    httpUpdate.onProgress([](int current, int total) {
        static int lastPercent = -1;
        int percent = (current * 100) / total;
        if (percent != lastPercent && percent % 10 == 0) {
            Serial.printf("[OTA] Progress: %d%%\n", percent);
            lastPercent = percent;
        }
    });
    
    t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl.c_str());
    
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] ❌ OTA failed: %s\n", 
                httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No updates");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] ✅ OTA success — rebooting...");
            delay(1000);
            ESP.restart();
            break;
    }
}


// ============================================================
// ===== TASK 1: LED Blink =====
// ============================================================
void taskLED(void *parameter) {
    pinMode(LED_PIN, OUTPUT);
    
    Serial.println("[TASK-LED] Started on core " + String(xPortGetCoreID()));
    
    for (;;) {
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay(LED_INTERVAL_MS / portTICK_PERIOD_MS);
        
        digitalWrite(LED_PIN, LOW);
        vTaskDelay(LED_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

// ============================================================
// ===== TASK 2: Serial Output =====
// ============================================================
void taskSerial(void *parameter) {
    Serial.println("[TASK-SERIAL] Started on core " + String(xPortGetCoreID()));
    
    uint32_t counter = 0;
    
    for (;;) {
        Serial.printf("[TASK-SERIAL] Counter: %u | Uptime: %lu ms | Free heap: %u bytes\n",
            counter++,
            millis(),
            ESP.getFreeHeap()
        );
        
        vTaskDelay(SERIAL_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

// ============================================================
// ===== TASK 3: OTA Check =====
// ============================================================
void taskOTA(void *parameter) {
    Serial.println("[TASK-OTA] Started on core " + String(xPortGetCoreID()));
    
    // Wait until WiFi is connected
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    Serial.println("[TASK-OTA] WiFi connected, starting OTA check loop");
    
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            if (fetchManifest() && isUpdateAvailable()) {
                Serial.println("[TASK-OTA] 🔄 Update available!");
                performOTA();
            } else {
                Serial.println("[TASK-OTA] ✅ Already up to date");
            }
        } else {
            Serial.println("[TASK-OTA] ⚠️ WiFi disconnected");
        }
        
        vTaskDelay(OTA_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

// ===== Setup =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== ESP32 FreeRTOS + OTA Demo ===");
    Serial.printf("Current version: %s\n", CURRENT_VERSION);
    Serial.printf("Total cores: %d\n", portNUM_PROCESSORS);
    
    connectWiFi();
    
    // ===== Create Task 1: LED (Core 1) =====
    xTaskCreatePinnedToCore(
        taskLED,           // Task function
        "TaskLED",         // Task name
        2048,              // Stack size (bytes)
        NULL,              // Parameter
        1,                 // Priority (1 = low)
        NULL,              // Handle
        1                  // Core 1
    );
    
    // ===== Create Task 2: Serial (Core 0) =====
    xTaskCreatePinnedToCore(
        taskSerial,
        "TaskSerial",
        4096,
        NULL,
        1,
        NULL,
        0                  // Core 0
    );
    
    // ===== Create Task 3: OTA (Core 0) =====
    xTaskCreatePinnedToCore(
        taskOTA,
        "TaskOTA",
        8192,              // Larger stack for HTTPS
        NULL,
        2,                 // Higher priority
        NULL,
        0                  // Core 0
    );
    
    Serial.println("All tasks created");
}

// ===== Loop (not used) =====
void loop() {
    // Empty — all work is done in tasks
    vTaskDelay(portMAX_DELAY);
}