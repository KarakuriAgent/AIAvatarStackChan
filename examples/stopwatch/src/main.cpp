#include <Arduino.h>
#include <M5Unified.h>

#include "AIAvatarStackChan.h"
#include "BuiltinAvatarImages.h"
#include "UserConfig.h"

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
static constexpr int16_t kFaceTapMinY = 140;
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
static void handleFinalMessage(const char* responseText, const char* voiceText);
static void updatePhysicalButtons();

static void configureStopWatchDisplay() {
    constexpr int16_t w = kDisplayWidth;
    constexpr int16_t h = kDisplayHeight;

    avatar.display().setImageFitMode(aiavatar::ImageFitMode::Cover);
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
    avatar.systemUI().setMenuHorizontalMargin(52);
    avatar.systemUI().setMenuItemHeight(58);
    avatar.systemUI().setMenuTextSize(2);
    avatar.systemUI().setMenuPadding(24, 16);
    avatar.systemUI().setVirtualButtonsEnabled(false);
    avatar.systemUI().setTouchPushToTalkEnabled(false);
    avatar.systemUI().onUnhandledTap(handleFaceTap);
}

static bool handleFaceTap(int16_t x, int16_t y) {
    (void)x;
    if (physicalPttActive) return false;
    if (y < kFaceTapMinY) return false;

    uint32_t now = millis();
    if (now - lastFaceTapMs < kFaceTapCooldownMs) return true;
    lastFaceTapMs = now;
    avatar.resetSleepTimer("face tap");

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

static void handleStartMessage(const char* text) {
    Serial.printf("[Main] User: %s\n", text ? text : "");
    if (!text || strncmp(text, kOpenClawResponsePrefix, strlen(kOpenClawResponsePrefix)) != 0) return;
    M5.Power.setVibration(kOpenClawVibrationLevel);
    vibrationUntilMs = millis() + kOpenClawVibrationMs;
}

static void handleFinalMessage(const char* responseText, const char* voiceText) {
    (void)responseText;
    Serial.printf("[Main] StackChan: %s\n", voiceText ? voiceText : "");
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
        avatar.resetSleepTimer("button B down");
        physicalPttActive = avatar.startPushToTalk();
    } else if (!pressed && physicalPttActive) {
        avatar.resetSleepTimer("button B up");
        avatar.endPushToTalk();
        physicalPttActive = false;
    }
}

static void updatePhysicalButtons() {
    if (M5.BtnA.wasClicked()) {
        avatar.resetSleepTimer("button A");
        avatar.cycleVolume();
    }
    if (M5.BtnB.wasClicked()) {
        avatar.resetSleepTimer("button B");
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
    while (!Serial && millis() - serialStart < 100) {
        delay(10);
    }
    Serial.println();
    Serial.println("[Main] boot");

    auto m5cfg = M5.config();
    M5.begin(m5cfg);
    Serial.println("[Main] M5 initialized");
    Serial.printf("[Main] heap=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());

    applyUserConfig(config);
    applyUserHardwareConfig();

    // Load built-in image resources.
    resources.setBuiltinAssets(aiavatar::kBuiltinAssets,
                               aiavatar::kBuiltinAssetsCount);

    // Configure display orientation.
    config.displayRotation = kStopWatchUprightRotation;
    currentDisplayRotation = config.displayRotation;
    configureStopWatchDisplay();
    avatar.onStart(handleStartMessage);
    avatar.onFinal(handleFinalMessage);

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
