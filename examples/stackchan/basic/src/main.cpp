#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>

#include "AIAvatarStackChan.h"

static aiavatar::Config config;
static aiavatar::AIAvatar avatar;

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

    if (SD.begin(GPIO_NUM_4, SPI, 25000000)) {
        Serial.println("[Main] SD mounted");
        config.loadFromSD();
    } else {
        Serial.println("[Main] SD not available; using built-in defaults");
    }

    if (config.wsHost[0] == '\0') {
        Serial.println("[Main] WS host is empty; running hardware/display only");
    }

    // Remove `avatar.useStackChan(config)` if you want to run CoreS3 without Stack-chan hardware
    avatar.useStackChan(config);

    if (!avatar.begin(config)) {
        Serial.println("[Main] AIAvatar init failed");
        while (true) delay(1000);
    }
}

void loop() {
    avatar.update();
    delay(1);
}
