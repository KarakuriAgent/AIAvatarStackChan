#include "OtaUpdater.h"

#include "FirmwareInfo.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <mbedtls/sha256.h>

namespace aiavatar {

namespace {

constexpr uint32_t kHttpTimeoutMs = 15000;
constexpr size_t kOtaBufferSize = 2048;
constexpr uint32_t kOtaTaskStackSize = 12288;

struct TaskParams {
    OtaUpdater* updater;
    OtaUpdater::Operation operation;
};

bool startsWithHttps(const char* url) {
    return url && strncmp(url, "https://", 8) == 0;
}

void bytesToHex(const uint8_t* bytes, size_t len, char* out, size_t outSize) {
    static constexpr char kHex[] = "0123456789abcdef";
    if (!out || outSize == 0) return;
    size_t needed = len * 2 + 1;
    if (outSize < needed) {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = kHex[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = kHex[bytes[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

}  // namespace

OtaUpdater::OtaUpdater()
    : status_(OtaUpdateStatus::Idle),
      progressPercent_(-1),
      updateAvailable_(false),
      changed_(false),
      taskHandle_(nullptr),
      trust_(nullptr) {
    manifestUrl_[0] = '\0';
    apiKey_[0] = '\0';
    statusMessage_[0] = '\0';
    clearManifest();
}

void OtaUpdater::begin(const Config& config, const OtaTrust* trust) {
    strlcpy(manifestUrl_, config.otaManifestUrl, sizeof(manifestUrl_));
    strlcpy(apiKey_, config.otaApiKey, sizeof(apiKey_));
    trust_ = trust;
    setStatus(OtaUpdateStatus::Idle, manifestUrl_[0] ? "未確認" : "OTA URL未設定", -1);
}

bool OtaUpdater::checkForUpdate() {
    if (busy()) return false;
    updateAvailable_ = false;
    return startTask(Operation::Check);
}

bool OtaUpdater::startUpdate() {
    if (busy() || !updateAvailable_ || !manifest_.firmwareUrl[0]) return false;
    return startTask(Operation::Update);
}

bool OtaUpdater::consumeChanged() {
    bool changed = changed_;
    changed_ = false;
    return changed;
}

bool OtaUpdater::busy() const {
    return taskHandle_ != nullptr || status_ == OtaUpdateStatus::Checking ||
           status_ == OtaUpdateStatus::Updating;
}

bool OtaUpdater::startTask(Operation operation) {
    if (!manifestUrl_[0]) {
        setStatus(OtaUpdateStatus::CheckFailed, "OTA URL未設定", -1);
        return false;
    }
    if (!WiFi.isConnected()) {
        setStatus(OtaUpdateStatus::CheckFailed, "Wi-Fi未接続", -1);
        return false;
    }

    auto* params = static_cast<TaskParams*>(malloc(sizeof(TaskParams)));
    if (!params) {
        setStatus(OtaUpdateStatus::CheckFailed, "タスク作成失敗", -1);
        return false;
    }
    params->updater = this;
    params->operation = operation;

    BaseType_t ok = xTaskCreatePinnedToCore(OtaUpdater::taskEntry,
                                            operation == Operation::Check ? "OTACheck" : "OTAUpdate",
                                            kOtaTaskStackSize, params, 1, &taskHandle_, 1);
    if (ok != pdPASS) {
        free(params);
        taskHandle_ = nullptr;
        setStatus(OtaUpdateStatus::CheckFailed, "タスク作成失敗", -1);
        return false;
    }
    return true;
}

void OtaUpdater::taskEntry(void* arg) {
    auto* params = static_cast<TaskParams*>(arg);
    OtaUpdater* updater = params ? params->updater : nullptr;
    Operation operation = params ? params->operation : Operation::Check;
    free(params);

    if (updater) updater->runTask(operation);
    vTaskDelete(nullptr);
}

void OtaUpdater::runTask(Operation operation) {
    if (operation == Operation::Check) {
        runCheck();
    } else {
        runUpdate();
    }
    taskHandle_ = nullptr;
    changed_ = true;
}

void OtaUpdater::runCheck() {
    setStatus(OtaUpdateStatus::Checking, "アップデート確認中", -1);
    OtaManifest next = {};
    if (!fetchManifest(next)) return;

    manifest_ = next;
    updateAvailable_ = isRemoteNewer(manifest_);
    if (updateAvailable_) {
        setStatus(OtaUpdateStatus::UpdateAvailable, "アップデートがあります", -1);
    } else {
        setStatus(OtaUpdateStatus::UpToDate, "最新です", -1);
    }
}

void OtaUpdater::runUpdate() {
    setStatus(OtaUpdateStatus::Updating, "アップデート開始", 0);
    if (!downloadAndApply(manifest_)) return;

    setStatus(OtaUpdateStatus::UpdateSucceeded, "完了: 再起動します", 100);
    delay(1500);
    ESP.restart();
}

bool OtaUpdater::fetchManifest(OtaManifest& manifest) {
    WiFiClientSecure client;
    configureClient(client);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, manifestUrl_)) {
        setStatus(OtaUpdateStatus::CheckFailed, "manifest接続失敗", -1);
        return false;
    }
    addAuthHeader(http);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "manifest取得失敗: %d", code);
        http.end();
        setStatus(OtaUpdateStatus::CheckFailed, msg, -1);
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        setStatus(OtaUpdateStatus::CheckFailed, "manifest JSON不正", -1);
        return false;
    }

    memset(&manifest, 0, sizeof(manifest));
    strlcpy(manifest.version, doc["version"] | "", sizeof(manifest.version));
    strlcpy(manifest.releaseDate, doc["release_date"] | "", sizeof(manifest.releaseDate));
    strlcpy(manifest.firmwareUrl, doc["firmware_url"] | "", sizeof(manifest.firmwareUrl));
    strlcpy(manifest.sha256, doc["sha256"] | "", sizeof(manifest.sha256));
    manifest.size = doc["size"] | 0;
    strlcpy(manifest.signatureKeyId, doc["signature_key_id"] | "",
            sizeof(manifest.signatureKeyId));
    strlcpy(manifest.signatureAlg, doc["signature_alg"] | "es256",
            sizeof(manifest.signatureAlg));
    strlcpy(manifest.signature, doc["signature"] | "", sizeof(manifest.signature));

    if (!manifest.version[0] || !manifest.firmwareUrl[0] || !manifest.sha256[0] || manifest.size == 0) {
        setStatus(OtaUpdateStatus::CheckFailed, "manifest項目不足", -1);
        return false;
    }
    if (!startsWithHttps(manifest.firmwareUrl)) {
        setStatus(OtaUpdateStatus::CheckFailed, "firmware URLがHTTPSではありません", -1);
        return false;
    }
    if (manifest.sha256[0] && strlen(manifest.sha256) != 64) {
        setStatus(OtaUpdateStatus::CheckFailed, "SHA256が不正です", -1);
        return false;
    }
    char signatureError[64] = {};
    if (!trust_ || !trust_->verifyManifest("firmware", manifest.version, manifest.releaseDate,
                                          manifest.firmwareUrl, manifest.size, manifest.sha256,
                                          manifest.signatureKeyId, manifest.signatureAlg,
                                          manifest.signature, signatureError,
                                          sizeof(signatureError))) {
        setStatus(OtaUpdateStatus::CheckFailed, signatureError[0] ? signatureError : "manifest署名検証失敗", -1);
        return false;
    }
    return true;
}

bool OtaUpdater::downloadAndApply(const OtaManifest& manifest) {
    if (!manifest.firmwareUrl[0] || manifest.size == 0) {
        setStatus(OtaUpdateStatus::UpdateFailed, "manifest未取得", -1);
        return false;
    }

    WiFiClientSecure client;
    configureClient(client);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, manifest.firmwareUrl)) {
        setStatus(OtaUpdateStatus::UpdateFailed, "firmware接続失敗", -1);
        return false;
    }
    addAuthHeader(http);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "firmware取得失敗: %d", code);
        http.end();
        setStatus(OtaUpdateStatus::UpdateFailed, msg, -1);
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength > 0 && static_cast<size_t>(contentLength) != manifest.size) {
        http.end();
        setStatus(OtaUpdateStatus::UpdateFailed, "firmwareサイズ不一致", -1);
        return false;
    }

    if (!Update.begin(manifest.size, U_FLASH)) {
        http.end();
        setStatus(OtaUpdateStatus::UpdateFailed, "OTA開始失敗", -1);
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[kOtaBufferSize];
    size_t written = 0;
    uint32_t lastDataMs = millis();
    int lastProgress = -1;

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    while (written < manifest.size) {
        if (!http.connected() && stream->available() == 0) {
            mbedtls_sha256_free(&sha);
            Update.abort();
            http.end();
            setStatus(OtaUpdateStatus::UpdateFailed, "接続が切断されました", -1);
            return false;
        }

        int available = stream->available();
        if (available <= 0) {
            if (millis() - lastDataMs > kHttpTimeoutMs) {
                mbedtls_sha256_free(&sha);
                Update.abort();
                http.end();
                setStatus(OtaUpdateStatus::UpdateFailed, "ダウンロードタイムアウト", -1);
                return false;
            }
            delay(10);
            continue;
        }

        size_t remaining = manifest.size - written;
        size_t toRead = available;
        if (toRead > sizeof(buffer)) toRead = sizeof(buffer);
        if (toRead > remaining) toRead = remaining;

        size_t readLen = stream->readBytes(buffer, toRead);
        if (readLen == 0) continue;
        lastDataMs = millis();

        size_t writeLen = Update.write(buffer, readLen);
        if (writeLen != readLen) {
            mbedtls_sha256_free(&sha);
            Update.abort();
            http.end();
            setStatus(OtaUpdateStatus::UpdateFailed, "書き込み失敗", -1);
            return false;
        }

        mbedtls_sha256_update(&sha, buffer, readLen);
        written += readLen;
        int progress = static_cast<int>((written * 100) / manifest.size);
        if (progress != lastProgress) {
            lastProgress = progress;
            char msg[48];
            snprintf(msg, sizeof(msg), "ダウンロード中 %d%%", progress);
            setProgress(progress, msg);
        }
    }

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    http.end();

    if (manifest.sha256[0]) {
        char actual[65];
        bytesToHex(digest, sizeof(digest), actual, sizeof(actual));
        if (!equalsIgnoreCase(actual, manifest.sha256)) {
            Update.abort();
            setStatus(OtaUpdateStatus::UpdateFailed, "SHA256不一致", -1);
            return false;
        }
    }

    setProgress(100, "検証中");
    if (!Update.end()) {
        setStatus(OtaUpdateStatus::UpdateFailed, "OTA終了失敗", -1);
        return false;
    }
    if (!Update.isFinished()) {
        setStatus(OtaUpdateStatus::UpdateFailed, "OTA未完了", -1);
        return false;
    }
    return true;
}

void OtaUpdater::setStatus(OtaUpdateStatus status, const char* message, int progress) {
    status_ = status;
    if (progress >= -1) progressPercent_ = progress;
    strlcpy(statusMessage_, message ? message : "", sizeof(statusMessage_));
    changed_ = true;
}

void OtaUpdater::setProgress(int progress, const char* message) {
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    progressPercent_ = progress;
    if (message) strlcpy(statusMessage_, message, sizeof(statusMessage_));
    changed_ = true;
}

void OtaUpdater::clearManifest() {
    memset(&manifest_, 0, sizeof(manifest_));
}

void OtaUpdater::configureClient(WiFiClientSecure& client) const {
    client.setInsecure();
}

void OtaUpdater::addAuthHeader(HTTPClient& http) const {
    if (!apiKey_[0]) return;
    char header[192];
    snprintf(header, sizeof(header), "Bearer %s", apiKey_);
    http.addHeader("Authorization", header);
}

int OtaUpdater::compareVersion(const char* lhs, const char* rhs) const {
    if (!lhs) lhs = "";
    if (!rhs) rhs = "";
    const char* a = lhs;
    const char* b = rhs;
    while (*a || *b) {
        while (*a && !isdigit(static_cast<unsigned char>(*a))) ++a;
        while (*b && !isdigit(static_cast<unsigned char>(*b))) ++b;
        if (!*a || !*b) break;
        char* nextA = nullptr;
        char* nextB = nullptr;
        long av = strtol(a, &nextA, 10);
        long bv = strtol(b, &nextB, 10);
        a = nextA ? nextA : a;
        b = nextB ? nextB : b;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return strcmp(lhs, rhs);
}

bool OtaUpdater::isRemoteNewer(const OtaManifest& manifest) const {
    return compareVersion(manifest.version, kFirmwareVersion) > 0;
}

bool OtaUpdater::equalsIgnoreCase(const char* lhs, const char* rhs) const {
    if (!lhs || !rhs) return false;
    while (*lhs && *rhs) {
        if (tolower(static_cast<unsigned char>(*lhs)) !=
            tolower(static_cast<unsigned char>(*rhs))) {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

}  // namespace aiavatar
