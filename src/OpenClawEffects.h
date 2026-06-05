#pragma once

#include "LedController.h"
#include "ScreenRenderer.h"

#include <M5Unified.h>
#include <cstdint>

namespace aiavatar {

class OpenClawEffects {
public:
    static constexpr const char* kToolName = "send_query_to_openclaw";

    OpenClawEffects();

    void begin(ScreenRenderer& display, LedController& leds);
    void setEnabled(bool enabled);
    bool handleToolCall(const char* toolName);
    bool handleResponseStart(const char* text);
    void update();
    void draw(LGFX_Sprite* canvas) const;
    void preload();

private:
    static constexpr const char* kResponseMarker =
        "$OpenClaw returned a response. Deliver it to the user in a format and length suitable for the channel:";
    static constexpr uint32_t kImageIntroMs = 150;
    static constexpr uint32_t kImageMainMs = 2500;
    static constexpr uint32_t kImageOutroMs = 150;
    static constexpr uint32_t kLedDurationMs = 3000;
    static constexpr uint32_t kLedStepMs = 90;

    enum class ImagePhase : uint8_t {
        Idle,
        Intro,
        Main,
        Outro,
    };

    ScreenRenderer* display_;
    LedController* leds_;
    bool enabled_;
    bool ledActive_;
    uint32_t ledStartMs_;
    uint32_t ledLastStepMs_;
    uint8_t ledStep_;
    bool spritesLoaded_;
    SpriteLayer sprite01_;
    SpriteLayer sprite02_;
    ImagePhase imagePhase_;
    uint32_t imagePhaseStartMs_;

    void start();
    void loadSprites();
    void updateLed();
    void updateImage();
};

}  // namespace aiavatar
