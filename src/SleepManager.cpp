#include "SleepManager.h"

#include "AIAvatar.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <cstring>

namespace aiavatar {

SleepManager::SleepManager()
    : avatar_(nullptr),
      config_(nullptr),
      lastActivityMs_(0),
      wifiWakeStartMs_(0),
      websocketWakeStartMs_(0),
      sleeping_(false),
      lastSpeakerPlaying_(false),
      wifiOffActive_(false),
      wifiSleepActive_(false),
      wifiWakeCheckPending_(false),
      websocketWakeCheckPending_(false),
      wakeActivityTriggered_(false),
      activeWifiMode_(SleepWifiMode::Sleep) {
    sleepWifiSsid_[0] = '\0';
    sleepWifiPass_[0] = '\0';
}

void SleepManager::begin(AIAvatar& avatar, const Config& config) {
    avatar_ = &avatar;
    config_ = &config;
    lastActivityMs_ = millis();
    sleeping_ = false;
    lastSpeakerPlaying_ = false;
    wifiOffActive_ = false;
    wifiSleepActive_ = false;
    wifiWakeCheckPending_ = false;
    websocketWakeCheckPending_ = false;
    wakeActivityTriggered_ = false;
    sleepWifiSsid_[0] = '\0';
    sleepWifiPass_[0] = '\0';
}

uint8_t SleepManager::currentDisplayBrightness() const {
    if (!config_) return 0;
    return sleeping_ ? config_->sleepDisplayBrightness : config_->displayBrightness;
}

void SleepManager::resetSleepTimer(const char* reason) {
    lastActivityMs_ = millis();
    if (sleeping_) {
        wakeActivityTriggered_ = true;
        wake(reason);
    }
}

void SleepManager::resetWakeActivity() {
    wakeActivityTriggered_ = false;
}

void SleepManager::wake(const char* reason) {
    if (!avatar_ || !config_) return;

    M5.Display.setBrightness(config_->displayBrightness);
    avatar_->display().setDirty();
    sleeping_ = false;

    if (wifiOffActive_) {
        exitWifiOff(reason);
    } else if (wifiSleepActive_) {
        exitWifiSleep(reason);
    }
    Serial.printf("[Sleep] wake: %s\n", reason ? reason : "activity");
}

void SleepManager::update() {
    if (!avatar_ || !config_ || !config_->sleepEnabled) return;

    uint32_t now = millis();
    updateSpeakerActivity();
    updateWifiWakeCheck();
    updateWebSocketWakeCheck();

    if (sleeping_) return;
    if (!avatar_->isStartupComplete()) {
        lastActivityMs_ = now;
        return;
    }
    if (avatar_->isPushToTalkActive() || avatar_->isServerProcessing() ||
        avatar_->speaker().isPlaying()) {
        lastActivityMs_ = now;
        return;
    }
    if (now - lastActivityMs_ < config_->sleepTimeoutMs) return;

    enterSleep();
}

void SleepManager::updateSpeakerActivity() {
    bool speakerPlaying = avatar_->speaker().isPlaying();
    uint32_t now = millis();
    if (speakerPlaying) {
        lastActivityMs_ = now;
    } else if (lastSpeakerPlaying_) {
        resetSleepTimer("speech ended");
        Serial.println("[Sleep] activity: speech ended");
    }
    lastSpeakerPlaying_ = speakerPlaying;
}

void SleepManager::enterSleep() {
    sleeping_ = true;
    M5.Display.setBrightness(config_->sleepDisplayBrightness);

    activeWifiMode_ = activeNetworkSleepWifiMode();
    if (activeWifiMode_ == SleepWifiMode::Off) {
        enterWifiOff();
    } else {
        enterWifiSleep();
    }
    Serial.printf("[Sleep] display brightness=%u after %u ms idle\n",
                  config_->sleepDisplayBrightness, config_->sleepTimeoutMs);
}

void SleepManager::enterWifiSleep() {
    if (wifiSleepActive_) return;

    WiFi.setSleep(true);
    wifiSleepActive_ = true;
    wifiWakeCheckPending_ = false;
    websocketWakeCheckPending_ = false;
    Serial.printf("[WiFiSleep] enter status=%d connected=%u ws=%u rssi=%d\n",
                  static_cast<int>(WiFi.status()),
                  WiFi.status() == WL_CONNECTED ? 1 : 0,
                  avatar_->isConnected() ? 1 : 0,
                  WiFi.RSSI());
}

void SleepManager::exitWifiSleep(const char* reason) {
    if (!wifiSleepActive_) return;

    WiFi.setSleep(false);
    wifiSleepActive_ = false;
    Serial.printf("[WiFiSleep] wake request reason=%s status=%d connected=%u ws=%u rssi=%d\n",
                  reason ? reason : "activity",
                  static_cast<int>(WiFi.status()),
                  WiFi.status() == WL_CONNECTED ? 1 : 0,
                  avatar_->isConnected() ? 1 : 0,
                  WiFi.RSSI());
    if (WiFi.status() == WL_CONNECTED && !avatar_->isConnected()) {
        websocketWakeStartMs_ = millis();
        websocketWakeCheckPending_ = true;
        avatar_->connectWebSocket();
    }
}

void SleepManager::captureActiveWifi() {
    String activeSsid = WiFi.SSID();
    sleepWifiSsid_[0] = '\0';
    sleepWifiPass_[0] = '\0';
    for (uint8_t i = 0; i < config_->wifiNetworkCount; ++i) {
        const auto& network = config_->wifiNetworks[i];
        if (activeSsid != network.ssid) continue;
        strlcpy(sleepWifiSsid_, network.ssid, sizeof(sleepWifiSsid_));
        strlcpy(sleepWifiPass_, network.pass, sizeof(sleepWifiPass_));
        return;
    }
    strlcpy(sleepWifiSsid_, config_->wifiSsid, sizeof(sleepWifiSsid_));
    strlcpy(sleepWifiPass_, config_->wifiPass, sizeof(sleepWifiPass_));
}

SleepWifiMode SleepManager::activeNetworkSleepWifiMode() const {
    String activeSsid = WiFi.SSID();
    for (uint8_t i = 0; i < config_->wifiNetworkCount; ++i) {
        const auto& network = config_->wifiNetworks[i];
        if (activeSsid != network.ssid) continue;
        if (network.sleepWifiModeConfigured) return network.sleepWifiMode;
        return config_->sleepWifiMode;
    }
    return config_->sleepWifiMode;
}

void SleepManager::enterWifiOff() {
    if (wifiOffActive_) return;

    captureActiveWifi();
    Serial.printf("[WiFiOff] enter ssid=%s status=%d connected=%u ws=%u rssi=%d\n",
                  sleepWifiSsid_,
                  static_cast<int>(WiFi.status()),
                  WiFi.status() == WL_CONNECTED ? 1 : 0,
                  avatar_->isConnected() ? 1 : 0,
                  WiFi.RSSI());

    avatar_->disconnectWebSocket();
    uint32_t startedAt = millis();
    while (avatar_->isConnected() && millis() - startedAt < 250) {
        delay(1);
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiOffActive_ = true;
    wifiWakeCheckPending_ = false;
    websocketWakeCheckPending_ = false;
}

void SleepManager::exitWifiOff(const char* reason) {
    if (!wifiOffActive_) return;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    if (sleepWifiSsid_[0]) {
        WiFi.begin(sleepWifiSsid_, sleepWifiPass_);
    }
    wifiOffActive_ = false;
    wifiWakeStartMs_ = millis();
    wifiWakeCheckPending_ = true;
    websocketWakeCheckPending_ = false;
    Serial.printf("[WiFiOff] wake request reason=%s ssid=%s status=%d connected=%u ws=%u rssi=%d\n",
                  reason ? reason : "activity",
                  sleepWifiSsid_,
                  static_cast<int>(WiFi.status()),
                  WiFi.status() == WL_CONNECTED ? 1 : 0,
                  avatar_->isConnected() ? 1 : 0,
                  WiFi.RSSI());
}

void SleepManager::updateWifiWakeCheck() {
    if (!wifiWakeCheckPending_) return;

    wl_status_t status = WiFi.status();
    uint32_t elapsed = millis() - wifiWakeStartMs_;
    if (status == WL_CONNECTED) {
        wifiWakeCheckPending_ = false;
        websocketWakeStartMs_ = millis();
        websocketWakeCheckPending_ = true;
        avatar_->connectWebSocket();
        Serial.printf("[WiFiOff] WiFi connected after %u ms ws=%u ip=%s rssi=%d\n",
                      elapsed,
                      avatar_->isConnected() ? 1 : 0,
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        return;
    }
    if (elapsed >= 5000) {
        wifiWakeCheckPending_ = false;
        Serial.printf("[WiFiOff] WiFi still not connected after %u ms status=%d ws=%u\n",
                      elapsed,
                      static_cast<int>(status),
                      avatar_->isConnected() ? 1 : 0);
    }
}

void SleepManager::updateWebSocketWakeCheck() {
    if (!websocketWakeCheckPending_) return;

    uint32_t elapsed = millis() - websocketWakeStartMs_;
    if (avatar_->isConnected()) {
        websocketWakeCheckPending_ = false;
        Serial.printf("[Sleep] WebSocket connected after %u ms\n", elapsed);
        return;
    }
    if (elapsed >= 10000) {
        websocketWakeCheckPending_ = false;
        Serial.printf("[Sleep] WebSocket still not connected after %u ms wifi=%d\n",
                      elapsed, static_cast<int>(WiFi.status()));
    }
}

}  // namespace aiavatar
