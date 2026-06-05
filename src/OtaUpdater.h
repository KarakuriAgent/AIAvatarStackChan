#pragma once

#include "Config.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace aiavatar {

enum class OtaUpdateStatus : uint8_t {
    Idle = 0,
    Checking,
    UpToDate,
    UpdateAvailable,
    CheckFailed,
    Updating,
    UpdateFailed,
    UpdateSucceeded,
};

struct OtaManifest {
    char version[32];
    char releaseDate[32];
    char firmwareUrl[256];
    char sha256[65];
    size_t size;
};

class OtaUpdater {
public:
    enum class Operation : uint8_t {
        Check,
        Update,
    };

    OtaUpdater();

    void begin(const Config& config);
    bool checkForUpdate();
    bool startUpdate();
    bool consumeChanged();

    OtaUpdateStatus status() const { return status_; }
    const char* statusMessage() const { return statusMessage_; }
    int progressPercent() const { return progressPercent_; }
    bool updateAvailable() const { return updateAvailable_; }
    bool busy() const;
    const OtaManifest& manifest() const { return manifest_; }

private:
    char manifestUrl_[256];
    char apiKey_[160];
    char caCert_[2048];
    OtaManifest manifest_;
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
    bool fetchManifest(OtaManifest& manifest);
    bool downloadAndApply(const OtaManifest& manifest);
    void setStatus(OtaUpdateStatus status, const char* message, int progress = -1);
    void setProgress(int progress, const char* message = nullptr);
    void clearManifest();
    void configureClient(WiFiClientSecure& client) const;
    void addAuthHeader(HTTPClient& http) const;
    int compareVersion(const char* lhs, const char* rhs) const;
    bool isRemoteNewer(const OtaManifest& manifest) const;
    bool equalsIgnoreCase(const char* lhs, const char* rhs) const;
};

}  // namespace aiavatar
