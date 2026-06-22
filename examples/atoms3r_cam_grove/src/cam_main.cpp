#if defined(ATOMS3R_CAM_GROVE_CHILD)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <esp_camera.h>
#include <img_converters.h>

namespace {

#ifndef ATOMS3R_CAM_CHILD_I2C_SDA
#define ATOMS3R_CAM_CHILD_I2C_SDA 2
#endif
#ifndef ATOMS3R_CAM_CHILD_I2C_SCL
#define ATOMS3R_CAM_CHILD_I2C_SCL 1
#endif

constexpr uint8_t kChildI2cSda = ATOMS3R_CAM_CHILD_I2C_SDA;
constexpr uint8_t kChildI2cScl = ATOMS3R_CAM_CHILD_I2C_SCL;
constexpr uint8_t kI2cAddress = 0x42;
constexpr size_t kMaxConfigJson = 4096;
constexpr size_t kMaxNetworks = 5;
constexpr uint32_t kWifiRetryIntervalMs = 10000;

enum FrameType : uint8_t {
    kFrameBegin = 1,
    kFrameData = 2,
    kFrameEnd = 3,
};

struct WifiCred {
    char ssid[64];
    char pass[64];
};

Preferences prefs;
WebServer server(80);
WifiCred networks[kMaxNetworks];
size_t networkCount = 0;
size_t activeNetworkIndex = 0;
char apiKey[512] = {};
char configBuf[kMaxConfigJson] = {};
volatile uint16_t configExpectedLen = 0;
volatile bool configPending = false;
volatile bool configReceiving = false;
volatile bool configOverflow = false;
bool cameraReady = false;
bool cameraInitAttempted = false;
bool configReady = false;
bool serverStarted = false;
uint32_t lastWifiRetryMs = 0;

camera_config_t cameraConfig = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = 21,
    .pin_sscb_sda = 12,
    .pin_sscb_scl = 9,
    .pin_d7 = 13,
    .pin_d6 = 11,
    .pin_d5 = 17,
    .pin_d4 = 4,
    .pin_d3 = 48,
    .pin_d2 = 46,
    .pin_d1 = 42,
    .pin_d0 = 3,
    .pin_vsync = 10,
    .pin_href = 14,
    .pin_pclk = 40,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size = FRAMESIZE_QQVGA,
    .jpeg_quality = 0,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
    .sccb_i2c_port = -1,
};

void saveConfigJson(const char* json) {
    prefs.begin("camtest", false);
    prefs.putString("config", json);
    prefs.end();
}

bool parseConfigJson(const char* json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[Cam] config parse failed: %s\n", err.c_str());
        return false;
    }

    networkCount = 0;
    const char* primarySsid = doc["wifi_ssid"] | "";
    const char* primaryPass = doc["wifi_pass"] | "";
    if (primarySsid[0] && networkCount < kMaxNetworks) {
        strlcpy(networks[networkCount].ssid, primarySsid, sizeof(networks[networkCount].ssid));
        strlcpy(networks[networkCount].pass, primaryPass, sizeof(networks[networkCount].pass));
        ++networkCount;
    }

    if (doc["wifi_networks"].is<JsonArray>()) {
        for (JsonObject item : doc["wifi_networks"].as<JsonArray>()) {
            if (networkCount >= kMaxNetworks) break;
            const char* ssid = item["ssid"] | "";
            if (!ssid[0]) continue;
            bool duplicate = false;
            for (size_t i = 0; i < networkCount; ++i) {
                if (strcmp(networks[i].ssid, ssid) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            strlcpy(networks[networkCount].ssid, ssid, sizeof(networks[networkCount].ssid));
            strlcpy(networks[networkCount].pass, item["pass"] | "",
                    sizeof(networks[networkCount].pass));
            ++networkCount;
        }
    }

    const char* cameraToken = doc["camera_token"] | "";
    if (!cameraToken[0]) cameraToken = doc["api_key"] | "";
    strlcpy(apiKey, cameraToken, sizeof(apiKey));
    configReady = networkCount > 0;
    activeNetworkIndex = 0;
    Serial.printf("[Cam] config ready=%s networks=%u key=%s\n",
                  configReady ? "yes" : "no", static_cast<unsigned>(networkCount),
                  apiKey[0] ? "set" : "empty");
    return configReady;
}

bool loadSavedConfig() {
    prefs.begin("camtest", true);
    String saved = prefs.getString("config", "");
    prefs.end();
    if (saved.isEmpty()) return false;
    if (saved.length() >= kMaxConfigJson) return false;
    strlcpy(configBuf, saved.c_str(), sizeof(configBuf));
    return parseConfigJson(configBuf);
}

void connectWifi() {
    if (!configReady || networkCount == 0) return;
    if (WiFi.status() == WL_CONNECTED) return;
    const WifiCred& cred = networks[activeNetworkIndex % networkCount];
    Serial.printf("[Cam] WiFi connecting: %s\n", cred.ssid);
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cred.ssid, cred.pass);
    activeNetworkIndex = (activeNetworkIndex + 1) % networkCount;
    lastWifiRetryMs = millis();
}

bool initCamera();

bool authorized() {
    if (!apiKey[0]) return true;
    if (server.hasArg("key") && server.arg("key") == apiKey) return true;
    if (server.header("X-Camera-Key") == apiKey) return true;
    server.send(401, "text/plain", "unauthorized");
    return false;
}

bool captureJpeg(uint8_t** out, size_t* outLen, uint8_t quality = 80) {
    if (!out || !outLen) return false;
    if (!cameraReady) {
        cameraInitAttempted = true;
        cameraReady = initCamera();
    }
    if (!cameraReady) return false;
    *out = nullptr;
    *outLen = 0;

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();
    if (!fb) return false;

    uint8_t* jpg = nullptr;
    size_t jpgLen = 0;
    bool ok = frame2jpg(fb, quality, &jpg, &jpgLen);
    esp_camera_fb_return(fb);
    if (!ok || !jpg) return false;

    *out = jpg;
    *outLen = jpgLen;
    return true;
}

void handleCamera() {
    if (!authorized()) return;
    uint8_t* jpg = nullptr;
    size_t jpgLen = 0;
    if (!captureJpeg(&jpg, &jpgLen)) {
        server.send(500, "text/plain", "capture failed");
        return;
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "image/jpeg", reinterpret_cast<const char*>(jpg), jpgLen);
    free(jpg);
}

void handleRoot() {
    String body = "<html><body><img src=\"/stream";
    if (apiKey[0]) {
        body += "?key=";
        body += apiKey;
    }
    body += "\" style=\"width:100%;image-rendering:pixelated\"></body></html>";
    server.send(200, "text/html", body);
}

void handleStatus() {
    JsonDocument doc;
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ip"] = WiFi.localIP().toString();
    doc["camera"] = cameraReady;
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

void handleStream() {
    if (!authorized()) return;
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-store");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        uint8_t* jpg = nullptr;
        size_t jpgLen = 0;
        if (!captureJpeg(&jpg, &jpgLen)) break;
        client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                      static_cast<unsigned>(jpgLen));
        client.write(jpg, jpgLen);
        client.print("\r\n");
        free(jpg);
        delay(120);
    }
}

void startServer() {
    if (serverStarted) return;
    const char* headers[] = {"X-Camera-Key"};
    server.collectHeaders(headers, 1);
    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/camera", HTTP_GET, handleCamera);
    server.on("/stream", HTTP_GET, handleStream);
    server.begin();
    serverStarted = true;
    Serial.printf("[Cam] HTTP server ready: http://%s/\n", WiFi.localIP().toString().c_str());
}

void onI2cReceive(int count) {
    if (count < 3) return;
    uint8_t type = Wire.read();
    uint16_t value = Wire.read();
    value |= static_cast<uint16_t>(Wire.read()) << 8;
    if (type == kFrameBegin) {
        configExpectedLen = value;
        configReceiving = value > 0 && value < kMaxConfigJson;
        configOverflow = !configReceiving;
        memset(configBuf, 0, sizeof(configBuf));
        return;
    }
    if (type == kFrameData && configReceiving) {
        uint16_t offset = value;
        while (Wire.available()) {
            if (offset >= kMaxConfigJson - 1) {
                configOverflow = true;
                Wire.read();
                continue;
            }
            configBuf[offset++] = static_cast<char>(Wire.read());
        }
        return;
    }
    if (type == kFrameEnd && configReceiving && !configOverflow) {
        if (configExpectedLen < kMaxConfigJson) {
            configBuf[configExpectedLen] = '\0';
            configPending = true;
        }
        configReceiving = false;
    }
}

void onI2cRequest() {
    char response[64];
    int state = 0;
    if (configReady) state = 1;
    if (WiFi.status() == WL_CONNECTED) state = 2;
    if (configOverflow) state = 9;
    snprintf(response, sizeof(response), "R,%d,%s,%d", state,
             WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0",
             apiKey[0] ? 1 : 0);
    Wire.write(reinterpret_cast<const uint8_t*>(response), strlen(response));
}

bool initCamera() {
    pinMode(18, OUTPUT);
    digitalWrite(18, LOW);
    delay(20);
    esp_err_t err = esp_camera_init(&cameraConfig);
    if (err != ESP_OK) {
        Serial.printf("[Cam] camera init failed: 0x%x\n", err);
        return false;
    }
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_framesize(sensor, FRAMESIZE_QQVGA);
        sensor->set_vflip(sensor, 1);
        sensor->set_hmirror(sensor, 1);
    }
    Serial.println("[Cam] camera ready");
    return true;
}

void appSetup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("[Cam] AtomS3R-CAM I2C child");

    pinMode(kChildI2cSda, INPUT_PULLUP);
    pinMode(kChildI2cScl, INPUT_PULLUP);
    Wire.setBufferSize(256);
    Wire.begin(kI2cAddress, kChildI2cSda, kChildI2cScl, 50000);
    Wire.onReceive(onI2cReceive);
    Wire.onRequest(onI2cRequest);
    Serial.printf("[Cam] I2C slave ready sda=%u scl=%u addr=0x%02x\n",
                  kChildI2cSda, kChildI2cScl, kI2cAddress);

    loadSavedConfig();
    if (configReady) connectWifi();
}

void appLoop() {
    if (configPending) {
        noInterrupts();
        configPending = false;
        interrupts();
        if (parseConfigJson(configBuf)) {
            saveConfigJson(configBuf);
            WiFi.disconnect(true);
            connectWifi();
        }
    }

    if (configReady && WiFi.status() != WL_CONNECTED &&
        millis() - lastWifiRetryMs >= kWifiRetryIntervalMs) {
        connectWifi();
    }
    if (WiFi.status() == WL_CONNECTED) {
        startServer();
        if (!cameraReady && !cameraInitAttempted) {
            cameraInitAttempted = true;
            cameraReady = initCamera();
        }
        server.handleClient();
    }
    delay(2);
}

}  // namespace

void setup() { appSetup(); }
void loop() { appLoop(); }

#endif
