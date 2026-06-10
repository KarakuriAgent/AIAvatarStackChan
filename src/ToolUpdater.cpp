#include "ToolUpdater.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mbedtls/sha256.h>

namespace aiavatar {

namespace {

constexpr uint32_t kHttpTimeoutMs = 15000;
constexpr size_t kDownloadBufferSize = 2048;
constexpr uint32_t kToolUpdateTaskStackSize = 12288;
constexpr const char* kTempPackagePath = "/tools.zip.tmp";
constexpr const char* kStagingDir = "/tools_update_tmp";
constexpr size_t kZipNameMax = 160;
constexpr size_t kSdPathMax = 224;
constexpr uint32_t kZipLocalHeaderSignature = 0x04034b50;
constexpr uint32_t kZipCentralHeaderSignature = 0x02014b50;
constexpr uint32_t kZipEndCentralDirectorySignature = 0x06054b50;
constexpr uint16_t kZipMethodStore = 0;

struct TaskParams {
    ToolUpdater* updater;
    ToolUpdater::Operation operation;
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

bool sdReady() {
    return SD.cardType() != CARD_NONE;
}

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool readExact(File& file, uint8_t* buffer, size_t len) {
    return file.read(buffer, len) == len;
}

bool skipBytes(File& file, uint32_t len) {
    return file.seek(file.position() + len);
}

bool endsWithSlash(const char* value) {
    if (!value || !value[0]) return false;
    size_t len = strlen(value);
    return value[len - 1] == '/' || value[len - 1] == '\\';
}

void normalizeSlashes(char* value) {
    if (!value) return;
    for (char* p = value; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
}

bool isSafeZipPath(const char* path) {
    if (!path || !path[0]) return false;
    if (path[0] == '/' || path[0] == '\\' || strchr(path, ':')) return false;

    const char* segment = path;
    for (const char* p = path;; ++p) {
        char c = *p;
        if (static_cast<unsigned char>(c) < 0x20 && c != '\0') return false;
        if (c == '/' || c == '\\' || c == '\0') {
            size_t len = static_cast<size_t>(p - segment);
            if (len == 0) return c == '\0' && p > path && endsWithSlash(path);
            if ((len == 1 && segment[0] == '.') ||
                (len == 2 && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if (c == '\0') return true;
            segment = p + 1;
        }
    }
}

bool childPathFromFile(const char* parent, File& child, char* out, size_t outSize) {
    const char* name = child.name();
    if (!parent || !name || !name[0] || !out || outSize == 0) return false;
    int written = 0;
    if (name[0] == '/') {
        written = snprintf(out, outSize, "%s", name);
    } else if (strchr(name, '/')) {
        written = snprintf(out, outSize, "/%s", name);
    } else {
        written = snprintf(out, outSize, "%s/%s", parent, name);
    }
    return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace

ToolUpdater::ToolUpdater()
    : status_(OtaUpdateStatus::Idle),
      progressPercent_(-1),
      updateAvailable_(false),
      changed_(false),
      taskHandle_(nullptr),
      trust_(nullptr) {
    manifestUrl_[0] = '\0';
    apiKey_[0] = '\0';
    outputPath_[0] = '\0';
    versionPath_[0] = '\0';
    localVersion_[0] = '\0';
    statusMessage_[0] = '\0';
    clearManifest();
}

void ToolUpdater::begin(const Config& config, const OtaTrust* trust, const char* outputPath, const char* versionPath) {
    strlcpy(manifestUrl_, config.toolManifestUrl, sizeof(manifestUrl_));
    strlcpy(apiKey_, config.otaApiKey, sizeof(apiKey_));
    trust_ = trust;
    strlcpy(outputPath_, outputPath && outputPath[0] ? outputPath : "/tools.json",
            sizeof(outputPath_));
    strlcpy(versionPath_, versionPath && versionPath[0] ? versionPath : "/tools.version.json",
            sizeof(versionPath_));
    loadLocalVersion();
    setStatus(OtaUpdateStatus::Idle, manifestUrl_[0] ? "未確認" : "ツール更新URL未設定", -1);
}

bool ToolUpdater::checkForUpdate() {
    if (busy()) return false;
    updateAvailable_ = false;
    return startTask(Operation::Check);
}

bool ToolUpdater::startUpdate() {
    if (busy() || !updateAvailable_ || !manifest_.toolsUrl[0]) return false;
    return startTask(Operation::Update);
}

bool ToolUpdater::consumeChanged() {
    bool changed = changed_;
    changed_ = false;
    return changed;
}

bool ToolUpdater::busy() const {
    return taskHandle_ != nullptr || status_ == OtaUpdateStatus::Checking ||
           status_ == OtaUpdateStatus::Updating;
}

bool ToolUpdater::startTask(Operation operation) {
    if (!manifestUrl_[0]) {
        setStatus(OtaUpdateStatus::CheckFailed, "ツール更新URL未設定", -1);
        return false;
    }
    if (!sdReady()) {
        setStatus(OtaUpdateStatus::CheckFailed, "SD未使用", -1);
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

    BaseType_t ok = xTaskCreatePinnedToCore(
        ToolUpdater::taskEntry,
        operation == Operation::Check ? "ToolCheck" : "ToolUpdate",
        kToolUpdateTaskStackSize, params, 1, &taskHandle_, 1);
    if (ok != pdPASS) {
        free(params);
        taskHandle_ = nullptr;
        setStatus(OtaUpdateStatus::CheckFailed, "タスク作成失敗", -1);
        return false;
    }
    return true;
}

void ToolUpdater::taskEntry(void* arg) {
    auto* params = static_cast<TaskParams*>(arg);
    ToolUpdater* updater = params ? params->updater : nullptr;
    Operation operation = params ? params->operation : Operation::Check;
    free(params);

    if (updater) updater->runTask(operation);
    vTaskDelete(nullptr);
}

void ToolUpdater::runTask(Operation operation) {
    if (operation == Operation::Check) {
        runCheck();
    } else {
        runUpdate();
    }
    taskHandle_ = nullptr;
    changed_ = true;
}

void ToolUpdater::runCheck() {
    setStatus(OtaUpdateStatus::Checking, "ツール更新確認中", -1);
    loadLocalVersion();
    ToolManifest next = {};
    if (!fetchManifest(next)) return;

    manifest_ = next;
    updateAvailable_ = isRemoteNewer(manifest_);
    if (updateAvailable_) {
        setStatus(OtaUpdateStatus::UpdateAvailable, "ツール更新があります", -1);
    } else {
        setStatus(OtaUpdateStatus::UpToDate, "最新です", -1);
    }
}

void ToolUpdater::runUpdate() {
    setStatus(OtaUpdateStatus::Updating, "ツール更新開始", 0);
    if (!downloadAndApply(manifest_)) return;

    updateAvailable_ = false;
    strlcpy(localVersion_, manifest_.version, sizeof(localVersion_));
    setStatus(OtaUpdateStatus::UpdateSucceeded, "完了: 再読み込みします", 100);
}

bool ToolUpdater::fetchManifest(ToolManifest& manifest) {
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
    strlcpy(manifest.toolsUrl, doc["package_url"] | (doc["tools_url"] | (doc["url"] | "")),
            sizeof(manifest.toolsUrl));
    strlcpy(manifest.sha256, doc["sha256"] | "", sizeof(manifest.sha256));
    manifest.size = doc["size"] | 0;
    strlcpy(manifest.signatureKeyId, doc["signature_key_id"] | "",
            sizeof(manifest.signatureKeyId));
    strlcpy(manifest.signatureAlg, doc["signature_alg"] | "es256",
            sizeof(manifest.signatureAlg));
    strlcpy(manifest.signature, doc["signature"] | "", sizeof(manifest.signature));

    if (!manifest.version[0] || !manifest.toolsUrl[0] || !manifest.sha256[0] || manifest.size == 0) {
        setStatus(OtaUpdateStatus::CheckFailed, "manifest項目不足", -1);
        return false;
    }
    if (!startsWithHttps(manifest.toolsUrl)) {
        setStatus(OtaUpdateStatus::CheckFailed, "package URLがHTTPSではありません", -1);
        return false;
    }
    if (manifest.sha256[0] && strlen(manifest.sha256) != 64) {
        setStatus(OtaUpdateStatus::CheckFailed, "SHA256が不正です", -1);
        return false;
    }
    char signatureError[64] = {};
    if (!trust_ || !trust_->verifyManifest("tools", manifest.version, manifest.releaseDate,
                                          manifest.toolsUrl, manifest.size, manifest.sha256,
                                          manifest.signatureKeyId, manifest.signatureAlg,
                                          manifest.signature, signatureError,
                                          sizeof(signatureError))) {
        setStatus(OtaUpdateStatus::CheckFailed, signatureError[0] ? signatureError : "manifest署名検証失敗", -1);
        return false;
    }
    return true;
}

bool ToolUpdater::downloadAndApply(const ToolManifest& manifest) {
    if (!manifest.toolsUrl[0] || manifest.size == 0) {
        setStatus(OtaUpdateStatus::UpdateFailed, "manifest未取得", -1);
        return false;
    }

    SD.remove(kTempPackagePath);

    WiFiClientSecure client;
    configureClient(client);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, manifest.toolsUrl)) {
        setStatus(OtaUpdateStatus::UpdateFailed, "package接続失敗", -1);
        return false;
    }
    addAuthHeader(http);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "package取得失敗: %d", code);
        http.end();
        setStatus(OtaUpdateStatus::UpdateFailed, msg, -1);
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength > 0 && static_cast<size_t>(contentLength) != manifest.size) {
        http.end();
        setStatus(OtaUpdateStatus::UpdateFailed, "packageサイズ不一致", -1);
        return false;
    }

    File out = SD.open(kTempPackagePath, FILE_WRITE);
    if (!out) {
        http.end();
        setStatus(OtaUpdateStatus::UpdateFailed, "一時ファイル作成失敗", -1);
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[kDownloadBufferSize];
    size_t written = 0;
    uint32_t lastDataMs = millis();
    int lastProgress = -1;

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    while (written < manifest.size) {
        if (!http.connected() && stream->available() == 0) {
            mbedtls_sha256_free(&sha);
            out.close();
            SD.remove(kTempPackagePath);
            http.end();
            setStatus(OtaUpdateStatus::UpdateFailed, "接続が切断されました", -1);
            return false;
        }

        int available = stream->available();
        if (available <= 0) {
            if (millis() - lastDataMs > kHttpTimeoutMs) {
                mbedtls_sha256_free(&sha);
                out.close();
                SD.remove(kTempPackagePath);
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

        size_t writeLen = out.write(buffer, readLen);
        if (writeLen != readLen) {
            mbedtls_sha256_free(&sha);
            out.close();
            SD.remove(kTempPackagePath);
            http.end();
            setStatus(OtaUpdateStatus::UpdateFailed, "SD書き込み失敗", -1);
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

    out.close();
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (manifest.sha256[0]) {
        char actual[65];
        bytesToHex(digest, sizeof(digest), actual, sizeof(actual));
        if (!equalsIgnoreCase(actual, manifest.sha256)) {
            SD.remove(kTempPackagePath);
            setStatus(OtaUpdateStatus::UpdateFailed, "SHA256不一致", -1);
            return false;
        }
    }

    setProgress(100, "展開検証中");
    removeRecursive(kStagingDir);
    if (!ensureDirectoryPath(kStagingDir)) {
        SD.remove(kTempPackagePath);
        setStatus(OtaUpdateStatus::UpdateFailed, "展開先作成失敗", -1);
        return false;
    }
    if (!extractPackage(kTempPackagePath, kStagingDir)) {
        removeRecursive(kStagingDir);
        SD.remove(kTempPackagePath);
        return false;
    }

    char stagedToolsPath[kSdPathMax];
    const char* outputEntry = outputPath_[0] == '/' ? outputPath_ + 1 : outputPath_;
    if (!buildPackagePath(kStagingDir, outputEntry, stagedToolsPath, sizeof(stagedToolsPath)) ||
        !validateToolsJson(stagedToolsPath)) {
        removeRecursive(kStagingDir);
        SD.remove(kTempPackagePath);
        setStatus(OtaUpdateStatus::UpdateFailed, "tools JSON不正", -1);
        return false;
    }

    setProgress(100, "SDへ展開中");
    if (!extractPackage(kTempPackagePath, "/")) {
        removeRecursive(kStagingDir);
        SD.remove(kTempPackagePath);
        return false;
    }
    if (!validateToolsJson(outputPath_)) {
        removeRecursive(kStagingDir);
        SD.remove(kTempPackagePath);
        setStatus(OtaUpdateStatus::UpdateFailed, "適用後JSON不正", -1);
        return false;
    }

    removeRecursive(kStagingDir);
    SD.remove(kTempPackagePath);
    if (!writeLocalVersion(manifest)) {
        setStatus(OtaUpdateStatus::UpdateFailed, "version保存失敗", -1);
        return false;
    }
    return true;
}

bool ToolUpdater::extractPackage(const char* packagePath, const char* destRoot) {
    File zip = SD.open(packagePath, FILE_READ);
    if (!zip) {
        setStatus(OtaUpdateStatus::UpdateFailed, "packageを開けません", -1);
        return false;
    }

    while (zip.available()) {
        uint8_t sigBuf[4];
        if (!readExact(zip, sigBuf, sizeof(sigBuf))) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zip読込失敗", -1);
            return false;
        }
        uint32_t sig = readLe32(sigBuf);
        if (sig == kZipCentralHeaderSignature || sig == kZipEndCentralDirectorySignature) {
            break;
        }
        if (sig != kZipLocalHeaderSignature) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zip形式不正", -1);
            return false;
        }

        uint8_t header[26];
        if (!readExact(zip, header, sizeof(header))) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zipヘッダ不正", -1);
            return false;
        }

        uint16_t flags = readLe16(header + 2);
        uint16_t method = readLe16(header + 4);
        uint32_t compressedSize = readLe32(header + 14);
        uint32_t uncompressedSize = readLe32(header + 18);
        uint16_t nameLen = readLe16(header + 22);
        uint16_t extraLen = readLe16(header + 24);

        if ((flags & 0x0009) != 0 || method != kZipMethodStore ||
            compressedSize != uncompressedSize || nameLen == 0 || nameLen >= kZipNameMax) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zipはstore形式のみ対応", -1);
            return false;
        }

        char entryName[kZipNameMax];
        if (!readExact(zip, reinterpret_cast<uint8_t*>(entryName), nameLen)) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zip名読込失敗", -1);
            return false;
        }
        entryName[nameLen] = '\0';
        normalizeSlashes(entryName);

        if (!isSafeZipPath(entryName)) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zip内パス不正", -1);
            return false;
        }
        if (!skipBytes(zip, extraLen)) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zip読込失敗", -1);
            return false;
        }

        char outPath[kSdPathMax];
        if (!buildPackagePath(destRoot, entryName, outPath, sizeof(outPath))) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "zipパス長すぎ", -1);
            return false;
        }

        if (endsWithSlash(entryName)) {
            if (!ensureDirectoryPath(outPath) || compressedSize != 0) {
                zip.close();
                setStatus(OtaUpdateStatus::UpdateFailed, "zipディレクトリ不正", -1);
                return false;
            }
            continue;
        }

        if (!ensureParentDirs(outPath)) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "展開先作成失敗", -1);
            return false;
        }

        SD.remove(outPath);
        File out = SD.open(outPath, FILE_WRITE);
        if (!out) {
            zip.close();
            setStatus(OtaUpdateStatus::UpdateFailed, "展開ファイル作成失敗", -1);
            return false;
        }

        uint8_t buffer[kDownloadBufferSize];
        uint32_t remaining = compressedSize;
        while (remaining > 0) {
            size_t toRead = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
            size_t readLen = zip.read(buffer, toRead);
            if (readLen == 0) {
                out.close();
                zip.close();
                setStatus(OtaUpdateStatus::UpdateFailed, "zipデータ不足", -1);
                return false;
            }
            if (out.write(buffer, readLen) != readLen) {
                out.close();
                zip.close();
                setStatus(OtaUpdateStatus::UpdateFailed, "展開書込失敗", -1);
                return false;
            }
            remaining -= readLen;
        }
        out.close();
    }

    zip.close();
    return true;
}

bool ToolUpdater::removeRecursive(const char* path) {
    if (!path || !path[0] || strcmp(path, "/") == 0) return false;
    if (!SD.exists(path)) return true;

    File entry = SD.open(path);
    if (!entry) return SD.remove(path);

    if (!entry.isDirectory()) {
        entry.close();
        return SD.remove(path);
    }

    while (true) {
        File child = entry.openNextFile();
        if (!child) break;

        char childPath[kSdPathMax];
        bool ok = childPathFromFile(path, child, childPath, sizeof(childPath));
        child.close();
        if (!ok || !removeRecursive(childPath)) {
            entry.close();
            return false;
        }
    }

    entry.close();
    return SD.rmdir(path);
}

bool ToolUpdater::ensureDirectoryPath(const char* path) {
    if (!path || !path[0]) return false;
    if (strcmp(path, "/") == 0) return true;

    char current[kSdPathMax];
    strlcpy(current, path, sizeof(current));
    size_t len = strlen(current);
    while (len > 1 && current[len - 1] == '/') {
        current[--len] = '\0';
    }

    for (char* p = current + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (!SD.exists(current) && !SD.mkdir(current)) {
            *p = '/';
            return false;
        }
        *p = '/';
    }

    if (!SD.exists(current) && !SD.mkdir(current)) return false;
    File dir = SD.open(current);
    bool ok = dir && dir.isDirectory();
    if (dir) dir.close();
    return ok;
}

bool ToolUpdater::ensureParentDirs(const char* path) {
    if (!path || !path[0]) return false;

    char parent[kSdPathMax];
    strlcpy(parent, path, sizeof(parent));
    char* slash = strrchr(parent, '/');
    if (!slash || slash == parent) return true;
    *slash = '\0';
    return ensureDirectoryPath(parent);
}

bool ToolUpdater::buildPackagePath(const char* root, const char* entryName, char* out,
                                   size_t outSize) {
    if (!root || !entryName || !entryName[0] || !out || outSize == 0) return false;
    const char* entry = entryName;
    while (*entry == '/') ++entry;

    int written = 0;
    if (strcmp(root, "/") == 0) {
        written = snprintf(out, outSize, "/%s", entry);
    } else {
        size_t rootLen = strlen(root);
        const char* sep = (rootLen > 0 && root[rootLen - 1] == '/') ? "" : "/";
        written = snprintf(out, outSize, "%s%s%s", root, sep, entry);
    }
    return written > 0 && static_cast<size_t>(written) < outSize;
}

bool ToolUpdater::validateToolsJson(const char* path) {
    File file = SD.open(path, FILE_READ);
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return false;
    return doc["categories"].is<JsonArray>();
}

bool ToolUpdater::loadLocalVersion() {
    localVersion_[0] = '\0';
    if (!sdReady() || !SD.exists(versionPath_)) return false;

    File file = SD.open(versionPath_, FILE_READ);
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return false;

    strlcpy(localVersion_, doc["version"] | "", sizeof(localVersion_));
    return localVersion_[0] != '\0';
}

bool ToolUpdater::writeLocalVersion(const ToolManifest& manifest) {
    SD.remove(versionPath_);
    File file = SD.open(versionPath_, FILE_WRITE);
    if (!file) return false;
    file.print("{\"version\":\"");
    file.print(manifest.version);
    file.print("\",\"release_date\":\"");
    file.print(manifest.releaseDate);
    file.print("\"}\n");
    file.close();
    return true;
}

void ToolUpdater::setStatus(OtaUpdateStatus status, const char* message, int progress) {
    status_ = status;
    if (progress >= -1) progressPercent_ = progress;
    strlcpy(statusMessage_, message ? message : "", sizeof(statusMessage_));
    changed_ = true;
}

void ToolUpdater::setProgress(int progress, const char* message) {
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    progressPercent_ = progress;
    if (message) strlcpy(statusMessage_, message, sizeof(statusMessage_));
    changed_ = true;
}

void ToolUpdater::clearManifest() {
    memset(&manifest_, 0, sizeof(manifest_));
}

void ToolUpdater::configureClient(WiFiClientSecure& client) const {
    client.setInsecure();
}

void ToolUpdater::addAuthHeader(HTTPClient& http) const {
    if (!apiKey_[0]) return;
    char header[192];
    snprintf(header, sizeof(header), "Bearer %s", apiKey_);
    http.addHeader("Authorization", header);
}

int ToolUpdater::compareVersion(const char* lhs, const char* rhs) const {
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

bool ToolUpdater::isRemoteNewer(const ToolManifest& manifest) const {
    return compareVersion(manifest.version, localVersion_) > 0;
}

bool ToolUpdater::equalsIgnoreCase(const char* lhs, const char* rhs) const {
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
