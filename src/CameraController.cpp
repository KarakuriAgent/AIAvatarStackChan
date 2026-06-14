#include "CameraController.h"

#include "Config.h"

#include <M5Unified.h>
#include <cstring>

#if defined(AIAVATAR_BOARD_ATOMS3)
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <mbedtls/sha256.h>
#endif

#if __has_include(<esp_camera.h>) && __has_include(<img_converters.h>)
#include <esp_camera.h>
#include <img_converters.h>
#define AIAVATAR_HAS_ESP_CAMERA 1
#else
#define AIAVATAR_HAS_ESP_CAMERA 0
#endif

namespace aiavatar {

#if defined(AIAVATAR_BOARD_ATOMS3)
namespace {
constexpr uint8_t kGroveSda = 2;
constexpr uint8_t kGroveScl = 1;
constexpr uint8_t kCamI2cAddress = 0x42;
constexpr uint16_t kCamHttpPort = 80;
constexpr uint32_t kConfigRetryMs = 5000;
constexpr uint32_t kStatusPollMs = 500;
constexpr size_t kMaxConfigJson = 4096;
constexpr size_t kMaxJpegBytes = 120 * 1024;
constexpr size_t kI2cChunkBytes = 24;

enum RemoteFrameType : uint8_t {
    kFrameBegin = 1,
    kFrameData = 2,
    kFrameEnd = 3,
};

TwoWire& cameraWire() {
    return Wire;
}

void sha256UpdateText(mbedtls_sha256_context& ctx, const char* text) {
    if (!text) text = "";
    mbedtls_sha256_update(&ctx, reinterpret_cast<const unsigned char*>(text), strlen(text));
    const unsigned char separator = '\n';
    mbedtls_sha256_update(&ctx, &separator, 1);
}

String makeRemoteAuthToken(const Config& config) {
    uint8_t digest[32] = {};
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);

    char mac[17] = {};
    uint64_t efuseMac = ESP.getEfuseMac();
    snprintf(mac, sizeof(mac), "%04x%08x",
             static_cast<unsigned>((efuseMac >> 32) & 0xffff),
             static_cast<unsigned>(efuseMac & 0xffffffff));

    sha256UpdateText(ctx, "aiavatar-camera-token-v1");
    sha256UpdateText(ctx, mac);
    sha256UpdateText(ctx, config.wifiSsid);
    sha256UpdateText(ctx, config.wifiPass);
    sha256UpdateText(ctx, config.apiKey);
    for (uint8_t i = 0; i < config.wifiNetworkCount; ++i) {
        sha256UpdateText(ctx, config.wifiNetworks[i].ssid);
        sha256UpdateText(ctx, config.wifiNetworks[i].pass);
    }

    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    char hex[65] = {};
    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest); ++i) {
        hex[i * 2] = kHex[digest[i] >> 4];
        hex[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return String(hex);
}

bool networkListed(const Config& config, const char* ssid) {
    if (!ssid || !ssid[0]) return false;
    for (uint8_t i = 0; i < config.wifiNetworkCount; ++i) {
        if (strcmp(config.wifiNetworks[i].ssid, ssid) == 0) return true;
    }
    return false;
}
}  // namespace
#else
#if AIAVATAR_HAS_ESP_CAMERA
static camera_config_t cameraConfig = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = -1,
    .pin_sscb_sda = 12,
    .pin_sscb_scl = 11,
    .pin_d7 = 47,
    .pin_d6 = 48,
    .pin_d5 = 16,
    .pin_d4 = 15,
    .pin_d3 = 42,
    .pin_d2 = 41,
    .pin_d1 = 40,
    .pin_d0 = 39,
    .pin_vsync = 46,
    .pin_href = 38,
    .pin_pclk = 45,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 0,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    .sccb_i2c_port = -1,
};
#endif
#endif

bool CameraController::begin() {
#if defined(AIAVATAR_BOARD_ATOMS3)
    Serial.println("[Camera] AtomS3 remote camera requires config");
    return false;
#elif AIAVATAR_HAS_ESP_CAMERA
    M5.In_I2C.release();
    esp_err_t err = esp_camera_init(&cameraConfig);
    if (err != ESP_OK) {
        Serial.printf("[Camera] init failed: 0x%x\n", err);
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    }

    ready_ = true;
    Serial.println("[Camera] init ok");
    return true;
#else
    Serial.println("[Camera] esp_camera unavailable");
    return false;
#endif
}

bool CameraController::begin(const Config& config) {
#if defined(AIAVATAR_BOARD_ATOMS3)
    return configure(config);
#else
    (void)config;
    return begin();
#endif
}

bool CameraController::configure(const Config& config) {
#if defined(AIAVATAR_BOARD_ATOMS3)
    if (!beginRemoteI2c()) return false;
    bool built = buildRemoteConfig(config);
    ready_ = built;
    if (built) {
        sendRemoteConfig(true);
    }
    return ready_;
#else
    (void)config;
    return ready_;
#endif
}

void CameraController::update() {
#if defined(AIAVATAR_BOARD_ATOMS3)
    if (!ready_ || !remoteI2cStarted_ || !remoteConfigBuilt_) return;
    pollRemoteStatus();
#endif
}

bool CameraController::captureJpeg(uint8_t** outBuf, size_t* outLen, uint8_t quality) {
    if (!ready_ || !outBuf || !outLen) return false;

#if defined(AIAVATAR_BOARD_ATOMS3)
    (void)quality;
    return captureRemoteJpeg(outBuf, outLen);
#elif AIAVATAR_HAS_ESP_CAMERA
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[Camera] capture failed");
        return false;
    }

    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;
    bool converted = frame2jpg(fb, quality, &jpgBuf, &jpgLen);
    esp_camera_fb_return(fb);

    if (!converted || !jpgBuf) {
        Serial.println("[Camera] JPEG conversion failed");
        return false;
    }

    *outBuf = jpgBuf;
    *outLen = jpgLen;
    return true;
#else
    return false;
#endif
}

#if defined(AIAVATAR_BOARD_ATOMS3)
bool CameraController::beginRemoteI2c() {
    if (remoteI2cStarted_) return true;
    pinMode(kGroveSda, INPUT_PULLUP);
    pinMode(kGroveScl, INPUT_PULLUP);
    cameraWire().setTimeOut(80);
    bool ok = cameraWire().begin(kGroveSda, kGroveScl, 50000);
    remoteI2cStarted_ = ok;
    Serial.printf("[Camera] AtomS3 remote I2C %s sda=%u scl=%u addr=0x%02x\n",
                  ok ? "ready" : "failed", kGroveSda, kGroveScl, kCamI2cAddress);
    return ok;
}

bool CameraController::buildRemoteConfig(const Config& config) {
    String authToken = makeRemoteAuthToken(config);
    JsonDocument doc;
    doc["wifi_ssid"] = config.wifiSsid;
    doc["wifi_pass"] = config.wifiPass;
    doc["camera_token"] = authToken;
    doc["api_key"] = authToken;

    JsonArray networks = doc["wifi_networks"].to<JsonArray>();
    if (config.wifiSsid[0] && !networkListed(config, config.wifiSsid)) {
        JsonObject primary = networks.add<JsonObject>();
        primary["ssid"] = config.wifiSsid;
        primary["pass"] = config.wifiPass;
        primary["name"] = "";
    }
    for (uint8_t i = 0; i < config.wifiNetworkCount; ++i) {
        if (!config.wifiNetworks[i].ssid[0]) continue;
        JsonObject item = networks.add<JsonObject>();
        item["ssid"] = config.wifiNetworks[i].ssid;
        item["pass"] = config.wifiNetworks[i].pass;
        item["name"] = config.wifiNetworks[i].name;
    }

    String nextJson;
    serializeJson(doc, nextJson);
    if (nextJson.length() >= kMaxConfigJson) {
        Serial.printf("[Camera] remote config too large: %u bytes\n",
                      static_cast<unsigned>(nextJson.length()));
        remoteConfigBuilt_ = false;
        return false;
    }

    if (nextJson != remoteConfigJson_) {
        remoteConfigJson_ = nextJson;
        remoteAuthToken_ = authToken;
        remoteIp_ = "";
        remoteState_ = -1;
        remoteLastConfigSentMs_ = 0;
    }
    remoteAuthToken_ = authToken;
    remoteConfigBuilt_ = true;
    return true;
}

bool CameraController::sendRemoteConfig(bool force) {
    if (!remoteI2cStarted_ || !remoteConfigBuilt_ || remoteConfigJson_.isEmpty()) return false;
    uint32_t now = millis();
    if (!force && remoteLastConfigSentMs_ != 0 && now - remoteLastConfigSentMs_ < kConfigRetryMs) {
        return true;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(remoteConfigJson_.c_str());
    size_t len = remoteConfigJson_.length();

    auto writeFrame = [](uint8_t type, uint16_t value, const uint8_t* data, size_t dataLen) -> bool {
        cameraWire().beginTransmission(kCamI2cAddress);
        cameraWire().write(type);
        cameraWire().write(static_cast<uint8_t>(value & 0xff));
        cameraWire().write(static_cast<uint8_t>((value >> 8) & 0xff));
        if (data && dataLen > 0) cameraWire().write(data, dataLen);
        uint8_t err = cameraWire().endTransmission();
        if (err != 0) {
            Serial.printf("[Camera] remote I2C write type=%u err=%u\n", type, err);
            return false;
        }
        delay(8);
        return true;
    };

    if (!writeFrame(kFrameBegin, static_cast<uint16_t>(len), nullptr, 0)) return false;
    for (size_t offset = 0; offset < len; offset += kI2cChunkBytes) {
        size_t chunk = len - offset;
        if (chunk > kI2cChunkBytes) chunk = kI2cChunkBytes;
        if (!writeFrame(kFrameData, static_cast<uint16_t>(offset), bytes + offset, chunk)) {
            return false;
        }
    }
    if (!writeFrame(kFrameEnd, ++remoteConfigGeneration_, nullptr, 0)) return false;

    remoteLastConfigSentMs_ = now;
    return true;
}

bool CameraController::pollRemoteStatus() {
    if (!remoteI2cStarted_) return false;
    uint32_t now = millis();
    if (remoteLastStatusMs_ != 0 && now - remoteLastStatusMs_ < kStatusPollMs) {
        return remoteState_ == 2 && remoteIp_.length() > 6;
    }
    remoteLastStatusMs_ = now;

    char buf[96] = {};
    size_t n = cameraWire().requestFrom(kCamI2cAddress, static_cast<uint8_t>(sizeof(buf) - 1));
    for (size_t i = 0; i < n && i < sizeof(buf) - 1; ++i) {
        buf[i] = static_cast<char>(cameraWire().read());
    }
    if (n == 0) {
        remoteState_ = -1;
        return false;
    }

    String status(buf);
    status.trim();
    if (!status.startsWith("R,")) {
        Serial.printf("[Camera] remote bad status: %s\n", status.c_str());
        remoteState_ = -1;
        return false;
    }

    int first = status.indexOf(',', 2);
    if (first < 0) return false;
    int second = status.indexOf(',', first + 1);
    String state = status.substring(2, first);
    String ip = second > 0 ? status.substring(first + 1, second) : status.substring(first + 1);
    remoteState_ = state.toInt();
    if (remoteState_ == 2 && ip.length() > 6) {
        if (ip != remoteIp_) {
            remoteIp_ = ip;
            Serial.printf("[Camera] remote ready ip=%s\n", remoteIp_.c_str());
        }
        return true;
    }

    return false;
}

bool CameraController::captureRemoteJpeg(uint8_t** outBuf, size_t* outLen) {
    *outBuf = nullptr;
    *outLen = 0;

    if ((remoteState_ != 2 || remoteIp_.length() <= 6) && !pollRemoteStatus()) {
        Serial.println("[Camera] remote capture skipped: camera child not ready");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Camera] remote capture skipped: parent WiFi not connected");
        return false;
    }

    String url = "http://" + remoteIp_ + ":" + String(kCamHttpPort) + "/camera";
    HTTPClient http;
    http.setTimeout(3500);
    if (!http.begin(url)) {
        Serial.println("[Camera] remote HTTP begin failed");
        return false;
    }
    if (!remoteAuthToken_.isEmpty()) {
        http.addHeader("X-Camera-Key", remoteAuthToken_);
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Camera] remote HTTP GET failed: %d\n", code);
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0 || static_cast<size_t>(len) > kMaxJpegBytes) {
        Serial.printf("[Camera] remote JPEG length invalid: %d\n", len);
        http.end();
        return false;
    }

    uint8_t* jpg = static_cast<uint8_t*>(ps_malloc(len));
    if (!jpg) jpg = static_cast<uint8_t*>(malloc(len));
    if (!jpg) {
        Serial.printf("[Camera] remote JPEG allocation failed: %d bytes\n", len);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t got = stream->readBytes(jpg, len);
    http.end();
    if (got != static_cast<size_t>(len)) {
        Serial.printf("[Camera] remote JPEG short read: %u/%d\n",
                      static_cast<unsigned>(got), len);
        free(jpg);
        return false;
    }

    *outBuf = jpg;
    *outLen = got;
    return true;
}
#endif

}  // namespace aiavatar
