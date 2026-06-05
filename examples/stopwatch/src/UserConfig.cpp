#include "UserConfig.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace {

void addWifiNetwork(aiavatar::Config& config, uint8_t index, const char* name,
                    const char* ssid, const char* pass,
                    aiavatar::SleepWifiMode sleepWifiMode) {
    if (index >= aiavatar::kMaxWifiNetworks) return;

    auto& network = config.wifiNetworks[index];
    strlcpy(network.name, name, sizeof(network.name));
    strlcpy(network.ssid, ssid, sizeof(network.ssid));
    strlcpy(network.pass, pass, sizeof(network.pass));
    network.sleepWifiMode = sleepWifiMode;
    network.sleepWifiModeConfigured = true;

    if (config.wifiNetworkCount <= index) {
        config.wifiNetworkCount = index + 1;
    }
    if (index == 0) {
        strlcpy(config.wifiSsid, ssid, sizeof(config.wifiSsid));
        strlcpy(config.wifiPass, pass, sizeof(config.wifiPass));
    }
}

}  // namespace

void applyUserConfig(aiavatar::Config& config) {
    addWifiNetwork(config, 0, "Home", "your-ssid", "your-pass",
                   aiavatar::SleepWifiMode::Sleep);
    // addWifiNetwork(config, 1, "Mobile", "your-mobile-ssid", "your-mobile-pass",
    //                aiavatar::SleepWifiMode::Sleep); // Add as needed.

    strlcpy(config.wsHost, "192.168.1.1", sizeof(config.wsHost));
    config.wsPort = 8000;
    strlcpy(config.wsPath, "/ws", sizeof(config.wsPath));
    strlcpy(config.userId, "stopwatch-user", sizeof(config.userId));

    config.speakerVolume = 255;
    config.audioNormalizeTargetPeak = 1.0f;
    config.audioNormalizeMaxGain = 8.0f;
    config.fastStartup = true;

    config.sleepEnabled = true;
    config.sleepTimeoutMs = 60000;
    config.sleepDisplayBrightness = 32;
    config.sleepWifiMode = aiavatar::SleepWifiMode::Sleep;
}

void applyUserHardwareConfig() {
    auto spkcfg = M5.Speaker.config();
    // If the speaker is too quiet, uncomment and tune this value.
    // Too high a value may cause distortion or speaker damage.
    // spkcfg.magnification = 4;
    M5.Speaker.config(spkcfg);
    M5.Speaker.setVolume(255);
    M5.Speaker.setAllChannelVolume(255);
    Serial.printf("[Main] speaker magnification=%u volume=%u\n",
                  M5.Speaker.config().magnification, M5.Speaker.getVolume());
}
