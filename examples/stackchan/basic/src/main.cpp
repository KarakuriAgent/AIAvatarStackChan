#include <Arduino.h>
#include <M5Unified.h>

#include "AIAvatarStackChan.h"

#if __has_include("BuiltinFirmwareAssets.h")
#include "BuiltinFirmwareAssets.h"
#define AIAVATAR_HAS_BUILTIN_FIRMWARE_ASSETS 1
#else
#define AIAVATAR_HAS_BUILTIN_FIRMWARE_ASSETS 0
#endif

static aiavatar::Config config;
static aiavatar::ResourceProvider resources;
static aiavatar::AIAvatar avatar;

static void applyBoardProfile(aiavatar::Config& cfg) {
#if defined(AIAVATAR_BOARD_ATOMS3)
    if (cfg.pttMaxSeconds > 2) cfg.pttMaxSeconds = 2;
    if (cfg.playbackQueueDepth > 32) cfg.playbackQueueDepth = 32;
    Serial.printf("[Main] AtomS3 profile pttMaxSeconds=%u playbackQueueDepth=%u\n",
                  cfg.pttMaxSeconds, static_cast<unsigned>(cfg.playbackQueueDepth));
#else
    (void)cfg;
#endif
}

static void configureBodyHardware() {
#if defined(AIAVATAR_BOARD_ATOMS3)
    Serial.println("[Main] AtomS3 profile: StackChan body hardware not attached");
#else
    // Remove this call if you want to run CoreS3 without Stack-chan hardware.
    avatar.useStackChan(config);
#endif
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

#if AIAVATAR_HAS_BUILTIN_FIRMWARE_ASSETS
    resources.setBuiltinAssets(aiavatar::kBuiltinFirmwareAssets,
                               aiavatar::kBuiltinFirmwareAssetsCount);
    Serial.println("[Main] built-in firmware assets registered");
#endif

    if (resources.beginSD(GPIO_NUM_4)) {
        Serial.println("[Main] SD mounted");
    } else {
        Serial.println("[Main] SD not available");
    }

    if (resources.loadConfig(config)) {
        Serial.println("[Main] config loaded");
    } else {
        Serial.println("[Main] config not found; using built-in defaults");
    }

    applyBoardProfile(config);

    if (config.wsHost[0] == '\0') {
        Serial.println("[Main] WS host is empty; running hardware/display only");
    }

    // Uncomment to show the neutral face and start PTT recording as early as possible.
    // Tradeoff: blink, mouth sprites, speaker, WebSocket, and other assets become ready gradually after boot.
    // config.fastStartup = true;

    configureBodyHardware();

    if (!avatar.begin(config, resources)) {
        Serial.println("[Main] AIAvatar init failed");
        while (true) delay(1000);
    }
}

void loop() {
    avatar.update();
    delay(1);
}
