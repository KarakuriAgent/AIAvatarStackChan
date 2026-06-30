#include <Arduino.h>
#include <M5Unified.h>

#include "AIAvatarStackChan.h"

#if defined(AIAVATAR_BOARD_ATOMS3)
static constexpr uint8_t kAtomS3DefaultDisplayBrightness = 160;
#endif

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
    if (cfg.pttMaxSeconds > 8) cfg.pttMaxSeconds = 8;
    if (cfg.playbackQueueDepth > 32) cfg.playbackQueueDepth = 32;
    cfg.displayRotation = 1;
    cfg.statusOverlayEnabled = false;
    cfg.fastStartup = true;
    Serial.printf("[Main] AtomS3 profile pttMaxSeconds=%u playbackQueueDepth=%u rotation=%u brightness=%u\n",
                  cfg.pttMaxSeconds, static_cast<unsigned>(cfg.playbackQueueDepth),
                  cfg.displayRotation, cfg.displayBrightness);
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

static void applyRuntimeBoardProfile() {}

static void showAtomS3DisplayProbe() {
#if defined(AIAVATAR_BOARD_ATOMS3) && defined(AIAVATAR_ATOMS3_DISPLAY_PROBE)
    M5.Display.setBrightness(255);
    const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
    const char* labels[] = {"RED", "GREEN", "BLUE", "WHITE"};
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        M5.Display.fillScreen(colors[i]);
        M5.Display.setTextColor(i == 3 ? TFT_BLACK : TFT_WHITE, colors[i]);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString(labels[i], M5.Display.width() / 2, M5.Display.height() / 2);
        delay(350);
    }
    M5.Display.setBrightness(kAtomS3DefaultDisplayBrightness);
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
#if defined(AIAVATAR_BOARD_ATOMS3)
    m5cfg.external_speaker.atomic_echo = true;
#endif
    M5.begin(m5cfg);
    Serial.println("[Main] M5 initialized");
#if defined(AIAVATAR_BOARD_ATOMS3)
    Serial.printf("[Main] M5 board=%d display=%dx%d\n",
                  static_cast<int>(M5.getBoard()), M5.Display.width(), M5.Display.height());
    {
        auto spkcfg = M5.Speaker.config();
        auto miccfg = M5.Mic.config();
        Serial.printf("[Main] AtomS3 Echo pins spk(bck=%d ws=%d dout=%d) mic(bck=%d ws=%d din=%d)\n",
                      spkcfg.pin_bck, spkcfg.pin_ws, spkcfg.pin_data_out,
                      miccfg.pin_bck, miccfg.pin_ws, miccfg.pin_data_in);
    }
    showAtomS3DisplayProbe();
#endif
    Serial.printf("[Main] heap=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());

#if AIAVATAR_HAS_BUILTIN_FIRMWARE_ASSETS
    resources.setBuiltinAssets(aiavatar::kBuiltinFirmwareAssets,
                               aiavatar::kBuiltinFirmwareAssetsCount);
    Serial.println("[Main] built-in firmware assets registered");
#endif

#if defined(AIAVATAR_BOARD_ATOMS3)
    Serial.println("[Main] SD skipped on AtomS3");
#else
    if (resources.beginSD(GPIO_NUM_4)) {
        Serial.println("[Main] SD mounted");
    } else {
        Serial.println("[Main] SD not available");
    }
#endif

    if (resources.loadConfig(config)) {
        Serial.println("[Main] config loaded");
    } else {
        Serial.println("[Main] config not found; using built-in defaults");
    }

    applyBoardProfile(config);
#if defined(AIAVATAR_BOARD_ATOMS3)
    avatar.display().setImageFitMode(aiavatar::ImageFitMode::Cover);
    Serial.println("[Main] AtomS3 display fit=cover");
#endif

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
    applyRuntimeBoardProfile();
}

void loop() {
    avatar.update();
    delay(1);
}
