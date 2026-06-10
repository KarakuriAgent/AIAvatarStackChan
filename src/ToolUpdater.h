#pragma once

#include "Config.h"
#include "OtaUpdater.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace aiavatar {

struct ToolManifest {
    char version[32];
    char releaseDate[32];
    char toolsUrl[256];
    char sha256[65];
    size_t size;
};

class ToolUpdater {
public:
    enum class Operation : uint8_t {
        Check,
        Update,
    };

    ToolUpdater();

    void begin(const Config& config, const char* outputPath = "/tools.json",
               const char* versionPath = "/tools.version.json");
    bool checkForUpdate();
    bool startUpdate();
    bool consumeChanged();

    OtaUpdateStatus status() const { return status_; }
    const char* statusMessage() const { return statusMessage_; }
    int progressPercent() const { return progressPercent_; }
    bool updateAvailable() const { return updateAvailable_; }
    bool busy() const;
    const ToolManifest& manifest() const { return manifest_; }
    const char* localVersion() const { return localVersion_; }

private:
    char manifestUrl_[256];
    char apiKey_[160];
    char caCert_[2048];
    char outputPath_[64];
    char versionPath_[64];
    char localVersion_[32];
    ToolManifest manifest_;
    char statusMessage_[128];
    volatile OtaUpdateStatus status_;
    volatile int progressPercent_;
    volatile bool updateAvailable_;
    volatile bool changed_;
    TaskHandle_t taskHandle_;

    bool startTask(Operation operation);
    static void taskEntry(void* arg);
    void runTask(Operation operation);
    void runCheck();
    void runUpdate();
    bool fetchManifest(ToolManifest& manifest);
    bool downloadAndApply(const ToolManifest& manifest);
    bool extractPackage(const char* packagePath, const char* destRoot);
    bool removeRecursive(const char* path);
    bool ensureDirectoryPath(const char* path);
    bool ensureParentDirs(const char* path);
    bool buildPackagePath(const char* root, const char* entryName, char* out, size_t outSize);
    bool validateToolsJson(const char* path);
    bool loadLocalVersion();
    bool writeLocalVersion(const ToolManifest& manifest);
    void setStatus(OtaUpdateStatus status, const char* message, int progress = -1);
    void setProgress(int progress, const char* message = nullptr);
    void clearManifest();
    void configureClient(WiFiClientSecure& client) const;
    void addAuthHeader(HTTPClient& http) const;
    int compareVersion(const char* lhs, const char* rhs) const;
    bool isRemoteNewer(const ToolManifest& manifest) const;
    bool equalsIgnoreCase(const char* lhs, const char* rhs) const;
};

}  // namespace aiavatar
