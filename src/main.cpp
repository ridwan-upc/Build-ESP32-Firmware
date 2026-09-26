#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

// ============================================================
// ===== Configuration (from build flags) =====
// ============================================================

// MANIFEST_URL is defined by build flags in platformio.ini.
// Fallback to production if not defined.
#ifndef MANIFEST_URL
  #define MANIFEST_URL "https://github.com/ridwan-upc/Build-ESP32-Firmware/releases/latest/download/manifest.json"
#endif

// VERSION_CHECK_MODE:
//   0 = compare SHA256 only (dev channel)
//   1 = compare version OR SHA256 (staging channel)
//   2 = compare version only (production channel)
#ifndef VERSION_CHECK_MODE
  #define VERSION_CHECK_MODE 2
#endif

// Default version — only used on first boot.
// After OTA, the version stored in NVS is used.
const char* DEFAULT_VERSION = "1.0.0";

#define LED_PIN 2               // ESP32 built-in LED
#define LED_INTERVAL_MS 500     // Blink every 500ms
#define SERIAL_INTERVAL_MS 1000 // Send serial every 1 second
#define OTA_INTERVAL_MS 3600000 // Check OTA every 1 hour

// ===== State =====
String currentVersion;
String currentSha256;
String latestVersion;
String latestSha256;
String firmwareUrl;

// ============================================================
// ===== NVS: Read current version =====
// ============================================================
String getCurrentVersion() {
    Preferences prefs;
    prefs.begin("firmware", true);  // read-only
    String version = prefs.getString("version", DEFAULT_VERSION);
    prefs.end();
    return version;
}

// ============================================================
// ===== NVS: Save new version =====
// ============================================================
void setCurrentVersion(const String& version) {
    Preferences prefs;
    prefs.begin("firmware", false);  // read-write
    prefs.putString("version", version);
    prefs.end();
    Serial.printf("[NVS] Version saved: %s\n", version.c_str());
}

// ============================================================
// ===== NVS: Read current SHA256 =====
// ============================================================
String getCurrentSha256() {
    Preferences prefs;
    prefs.begin("firmware", true);
    String sha = prefs.getString("sha256", "");
    prefs.end();
    return sha;
}

// ============================================================
// ===== NVS: Save new SHA256 =====
// ============================================================
void setCurrentSha256(const String& sha) {
    Preferences prefs;
    prefs.begin("firmware", false);
    prefs.putString("sha256", sha);
    prefs.end();
    Serial.printf("[NVS] SHA256 saved: %s\n", sha.c_str());
}

// ============================================================
// ===== Function: Connect WiFi via WiFiManager =====
// ============================================================
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

// ============================================================
// ===== Function: Fetch Manifest =====
// ============================================================
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
    latestSha256 = doc["sha256"].as<String>();
    
    Serial.printf("[OTA] Latest: %s | Current: %s\n", 
        latestVersion.c_str(), currentVersion.c_str());
    
    return true;
}

// ============================================================
// ===== Function: Check if update is available =====
// ============================================================
bool isUpdateAvailable() {
    #if VERSION_CHECK_MODE == 0
        // Dev channel: compare SHA256 only
        return latestSha256 != currentSha256;
    #elif VERSION_CHECK_MODE == 1
        // Staging channel: compare version OR SHA256
        return (latestVersion != currentVersion) || 
               (latestSha256 != currentSha256);
    #else
        // Production channel: compare version only
        return latestVersion != currentVersion;
    #endif
}

// ============================================================
// ===== Function: Perform OTA update =====
// ============================================================
void performOTA() {
    Serial.println("[OTA] Starting OTA update...");
    Serial.printf("[OTA] URL: %s\n", firmwareUrl.c_str());
    
    // HTTPS client
    WiFiClientSecure client;
    client.setInsecure();  // TODO: replace with setCACert() for production
    client.setTimeout(30);
    
    // HTTPUpdate with redirect support
    HTTPUpdate httpUpdate;
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(false);
    
    // Progress callback (optional, for debug)
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
            Serial.println("[OTA] ✅ OTA success — saving version and SHA256 to NVS...");
            
            // Save new version and SHA256 to NVS before reboot
            setCurrentVersion(latestVersion);
            setCurrentSha256(latestSha256);
            
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

// ============================================================
// ===== Setup =====
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== ESP32 FreeRTOS + OTA Demo ===");
    
    // Read version and SHA256 from NVS
    currentVersion = getCurrentVersion();
    currentSha256 = getCurrentSha256();
    Serial.printf("Current version: %s\n", currentVersion.c_str());
    Serial.printf("Current SHA256: %s\n", currentSha256.c_str());
    Serial.printf("Total cores: %d\n", portNUM_PROCESSORS);
    Serial.printf("Version check mode: %d\n", VERSION_CHECK_MODE);
    Serial.printf("Manifest URL: %s\n", MANIFEST_URL);
    
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

// ============================================================
// ===== Loop (not used) =====
// ============================================================
void loop() {
    // Empty — all work is done in tasks
    vTaskDelay(portMAX_DELAY);
}