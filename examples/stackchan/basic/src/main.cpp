#include <Arduino.h>
#include <M5Unified.h>

#include "AIAvatarStackChan.h"

static aiavatar::Config config;
static aiavatar::ResourceProvider resources;
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

    if (resources.beginSD(GPIO_NUM_4)) {
        Serial.println("[Main] SD mounted");
        resources.loadConfig(config);
    } else {
        Serial.println("[Main] SD not available; using built-in defaults");
    }

    if (config.wsHost[0] == '\0') {
        Serial.println("[Main] WS host is empty; check /config.json");
        while (true) delay(1000);
    }

    // Remove `avatar.useStackChan()` if you want to run CoreS3 without Stack-chan hardware
    avatar.useStackChan();

    if (!avatar.begin(config, resources)) {
        Serial.println("[Main] AIAvatar init failed");
        while (true) delay(1000);
    }
}

void loop() {
    avatar.update();
    delay(1);
}
