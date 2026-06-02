#pragma once

#include "Config.h"

#include <cstdint>

namespace aiavatar {

class AIAvatar;

class SleepManager {
public:
    SleepManager();

    void begin(AIAvatar& avatar, const Config& config);
    void noteActivity(const char* reason);
    void update();

    bool isSleeping() const { return sleeping_; }
    bool isWifiOff() const { return wifiOffActive_; }
    bool isWifiSleepActive() const { return wifiSleepActive_; }
    uint8_t currentDisplayBrightness() const;

private:
    AIAvatar* avatar_;
    const Config* config_;
    uint32_t lastActivityMs_;
    uint32_t wifiWakeStartMs_;
    uint32_t websocketWakeStartMs_;
    bool sleeping_;
    bool lastSpeakerPlaying_;
    bool wifiOffActive_;
    bool wifiSleepActive_;
    bool wifiWakeCheckPending_;
    bool websocketWakeCheckPending_;
    SleepWifiMode activeWifiMode_;
    char sleepWifiSsid_[64];
    char sleepWifiPass_[64];

    void wake(const char* reason);
    void enterSleep();
    void updateTouchActivity();
    void updateSpeakerActivity();
    void enterWifiSleep();
    void exitWifiSleep(const char* reason);
    void enterWifiOff();
    void exitWifiOff(const char* reason);
    void updateWifiWakeCheck();
    void updateWebSocketWakeCheck();
    void captureActiveWifi();
    SleepWifiMode activeNetworkSleepWifiMode() const;
};

}  // namespace aiavatar
