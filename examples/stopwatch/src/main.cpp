#include <Arduino.h>
#include <M5Unified.h>

#include "AIAvatarStackChan.h"
#include "BuiltinAvatarImages.h"

static aiavatar::Config config;
static aiavatar::ResourceProvider resources;
static aiavatar::AIAvatar avatar;
static bool physicalPttActive = false;
static uint8_t currentDisplayRotation = 0;
static uint32_t lastOrientationCheckMs = 0;

static constexpr int16_t kDisplayWidth = 466;
static constexpr int16_t kDisplayHeight = 466;
static constexpr uint8_t kStopWatchUprightRotation = 0;
static constexpr uint8_t kStopWatchUpsideDownRotation = 2;
static constexpr float kOrientationThreshold = 0.65f;
static constexpr uint32_t kOrientationCheckIntervalMs = 250;
static constexpr int16_t kFaceTapCenterX = kDisplayWidth / 2;
static constexpr int16_t kFaceTapCenterY = kDisplayHeight / 2 + 18;
static constexpr int16_t kFaceTapRadius = 142;
static constexpr uint32_t kFaceTapCooldownMs = 2000;
static constexpr uint32_t kOpenClawVibrationMs = 2000;
static constexpr uint8_t kOpenClawVibrationLevel = 255;
static constexpr const char* kOpenClawResponsePrefix =
    "$OpenClawから応答がありました。ユーザーに伝えてください";
// static constexpr const char* kOpenClawResponsePrefix =
//     "$OpenClaw has responded. Please tell the user";
static uint32_t lastFaceTapMs = 0;
static uint32_t vibrationUntilMs = 0;

static bool handleFaceTap(int16_t x, int16_t y);
static void handleStartMessage(const char* text);
static void updatePhysicalButtons();

static void addWifiNetwork(uint8_t index, const char* name, const char* ssid, const char* pass) {
    if (index >= aiavatar::kMaxWifiNetworks) return;

    auto& network = config.wifiNetworks[index];
    strlcpy(network.name, name, sizeof(network.name));
    strlcpy(network.ssid, ssid, sizeof(network.ssid));
    strlcpy(network.pass, pass, sizeof(network.pass));
    if (config.wifiNetworkCount <= index) {
        config.wifiNetworkCount = index + 1;
    }
    if (index == 0) {
        strlcpy(config.wifiSsid, ssid, sizeof(config.wifiSsid));
        strlcpy(config.wifiPass, pass, sizeof(config.wifiPass));
    }
}

static void configureStopWatchDisplay() {
    constexpr int16_t w = kDisplayWidth;
    constexpr int16_t h = kDisplayHeight;

    avatar.display().setImageFitMode(aiavatar::ImageFitMode::Cover);

    // TTSにあわせて調整してね
    avatar.speaker().setPcmGain(6.0f);

    avatar.visualEffects().setListeningGlowShape(aiavatar::ListeningGlowShape::Circle);
    avatar.visualEffects().setCircularListeningGlowWidth(10.5f);
    avatar.visualEffects().setCircularListeningGlowSeamlessGradient(true);

    avatar.statusOverlay().setClockPosition(w / 2, 86, top_center);
    avatar.statusOverlay().setClockTextSize(6);
    avatar.statusOverlay().setMicBounds({w / 2 - 72, 28, 36, 36});
    avatar.statusOverlay().setNetworkBounds({w / 2 - 18, 28, 36, 36});
    avatar.statusOverlay().setBatteryBounds({w / 2 + 36, 28, 36, 36});
    avatar.statusOverlay().setMicTapBounds({w / 2 - 94, 16, 62, 62});
    avatar.statusOverlay().setNetworkTapBounds({w / 2 - 31, 16, 62, 62});
    avatar.statusOverlay().setBatteryTapBounds({w / 2 + 32, 16, 62, 62});
    avatar.statusOverlay().setIconSize(42);
    avatar.statusOverlay().setWifiStrokeRadius(1);
    avatar.statusOverlay().setVolumeTapBounds({(w - 120) / 2, h - 96, 120, 64});
    avatar.statusOverlay().setVolumeIndicatorX(42);

    avatar.systemUI().setSystemBarHeight(118);
    avatar.systemUI().setMenuHorizontalMargin(82);
    avatar.systemUI().setVirtualButtonArea(aiavatar::ButtonId::A,
                                           {(w - 110) / 2, h - 96, 110, 64});
    avatar.systemUI().setVirtualButtonArea(aiavatar::ButtonId::B,
                                           {w / 2 - 128, h - 96, 110, 64});
    avatar.systemUI().setVirtualButtonArea(aiavatar::ButtonId::C,
                                           {w / 2 + 18, h - 96, 110, 64});
    avatar.systemUI().setButtonAction(aiavatar::ButtonId::A, aiavatar::ButtonAction::None);
    avatar.systemUI().setTouchPushToTalkEnabled(false);
    avatar.systemUI().onUnhandledTap(handleFaceTap);
}

static bool isFaceTap(int16_t x, int16_t y) {
    int32_t dx = x - kFaceTapCenterX;
    int32_t dy = y - kFaceTapCenterY;
    return dx * dx + dy * dy <= static_cast<int32_t>(kFaceTapRadius) * kFaceTapRadius;
}

static bool handleFaceTap(int16_t x, int16_t y) {
    if (physicalPttActive) return false;
    if (!isFaceTap(x, y)) return false;

    uint32_t now = millis();
    if (now - lastFaceTapMs < kFaceTapCooldownMs) return true;
    lastFaceTapMs = now;

    static constexpr const char* kPrompt =
        "$The user poked your face on the screen. React with one very short phrase. "
        "You may sound surprised, shy, or slightly annoyed depending on your personality.";
    if (avatar.invokeText(kPrompt)) {
        Serial.println("[Main] face tap invoke queued");
    } else {
        Serial.println("[Main] face tap invoke failed");
    }
    return true;
}

static bool startsWith(const char* text, const char* prefix) {
    if (!text || !prefix) return false;
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void handleStartMessage(const char* text) {
    if (!startsWith(text, kOpenClawResponsePrefix)) return;
    M5.Power.setVibration(kOpenClawVibrationLevel);
    vibrationUntilMs = millis() + kOpenClawVibrationMs;
}

static void updateVibration() {
    if (vibrationUntilMs == 0) return;
    if (static_cast<int32_t>(millis() - vibrationUntilMs) < 0) return;
    vibrationUntilMs = 0;
    M5.Power.setVibration(0);
}

static void updatePhysicalPushToTalk() {
    bool pressed = M5.BtnB.isPressed();

    if (pressed && !physicalPttActive) {
        physicalPttActive = avatar.startPushToTalk();
    } else if (!pressed && physicalPttActive) {
        avatar.endPushToTalk();
        physicalPttActive = false;
    }
}

static void updatePhysicalButtons() {
    if (M5.BtnA.wasClicked()) {
        avatar.cycleVolume();
    }
    updatePhysicalPushToTalk();
}

static void updateDisplayOrientation() {
    uint32_t now = millis();
    if (now - lastOrientationCheckMs < kOrientationCheckIntervalMs) return;
    lastOrientationCheckMs = now;
    if (!M5.Imu.isEnabled()) return;

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    uint8_t nextRotation = currentDisplayRotation;
    if (ax > kOrientationThreshold) {
        nextRotation = kStopWatchUpsideDownRotation;
    } else if (ax < -kOrientationThreshold) {
        nextRotation = kStopWatchUprightRotation;
    } else {
        return;
    }

    if (nextRotation != currentDisplayRotation && avatar.display().setRotation(nextRotation)) {
        currentDisplayRotation = nextRotation;
        Serial.printf("[Main] orientation rotation=%u accel=(%.2f,%.2f,%.2f)\n",
                      currentDisplayRotation, ax, ay, az);
    }
}

void setup() {
    Serial.begin(115200);
    uint32_t serialStart = millis();
    while (!Serial && millis() - serialStart < 3000) {
        delay(10);
    }
    delay(300);
    Serial.println();
    Serial.println("[Main] boot");

    auto m5cfg = M5.config();
    M5.begin(m5cfg);
    Serial.println("[Main] M5 initialized");
    Serial.printf("[Main] heap=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());

    // Minimum built-in settings.
    addWifiNetwork(0, "Home WiFi", "your-ssid", "your-pass");
    // addWifiNetwork(1, "Mobile", "your-mobile-ssid", "your-mobile-pass"); // Add as needed.
    strlcpy(config.wsHost, "192.168.1.1", sizeof(config.wsHost));
    config.wsPort = 8000;
    strlcpy(config.wsPath, "/ws", sizeof(config.wsPath));
    strlcpy(config.userId, "stopwatch-user", sizeof(config.userId));

    // Add extra settings here instead of config.json.
    config.speakerVolume = 255;

    // Load built-in image resources.
    resources.setBuiltinAssets(aiavatar::kBuiltinAssets,
                               aiavatar::kBuiltinAssetsCount);

    // Configure display orientation.
    config.displayRotation = kStopWatchUprightRotation;
    currentDisplayRotation = config.displayRotation;
    configureStopWatchDisplay();
    avatar.onStart(handleStartMessage);

    if (!avatar.begin(config, resources)) {
        Serial.println("[Main] AIAvatar init failed");
        while (true) delay(1000);
    }

    avatar.setMicMuted(true);
}

void loop() {
    avatar.update();
    updateVibration();
    updatePhysicalButtons();
    updateDisplayOrientation();
    delay(1);
}
