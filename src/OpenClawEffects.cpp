#include "OpenClawEffects.h"

#include <Arduino.h>
#include <cstring>

namespace aiavatar {

OpenClawEffects::OpenClawEffects()
    : display_(nullptr),
      leds_(nullptr),
      enabled_(true),
      ledActive_(false),
      ledStartMs_(0),
      ledLastStepMs_(0),
      ledStep_(0),
      spritesLoaded_(false),
      sprite01_{nullptr, 0, 0, ScreenRenderer::kTransparentColor},
      sprite02_{nullptr, 0, 0, ScreenRenderer::kTransparentColor},
      imagePhase_(ImagePhase::Idle),
      imagePhaseStartMs_(0) {}

void OpenClawEffects::begin(ScreenRenderer& display, LedController& leds) {
    display_ = &display;
    leds_ = &leds;
}

void OpenClawEffects::preload() {
    loadSprites();
}

void OpenClawEffects::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        ledActive_ = false;
        imagePhase_ = ImagePhase::Idle;
        if (display_) display_->setDirty();
    }
}

bool OpenClawEffects::handleToolCall(const char* toolName) {
    if (!enabled_ || !toolName || strcmp(toolName, kToolName) != 0) return false;
    Serial.println("[OpenClaw] tool detected");
    start();
    return true;
}

bool OpenClawEffects::handleResponseStart(const char* text) {
    if (!enabled_ || !text || !strstr(text, kResponseMarker)) return false;
    Serial.println("[OpenClaw] response detected");
    start();
    return true;
}

void OpenClawEffects::update() {
    if (!enabled_) return;
    updateLed();
    updateImage();
}

void OpenClawEffects::draw(LGFX_Sprite* canvas) const {
    if (!canvas || !enabled_ || imagePhase_ == ImagePhase::Idle) return;

    const SpriteLayer* sprite = nullptr;
    if (imagePhase_ == ImagePhase::Intro || imagePhase_ == ImagePhase::Outro) {
        sprite = &sprite01_;
    } else if (imagePhase_ == ImagePhase::Main) {
        sprite = &sprite02_;
    }
    if (sprite && sprite->valid()) {
        sprite->sprite->pushSprite(canvas, sprite->x, sprite->y, sprite->transparentColor);
    }
}

void OpenClawEffects::start() {
    if (!enabled_) return;

    if (leds_) {
        leds_->stopAnimation();
    }
    ledActive_ = true;
    ledStartMs_ = millis();
    ledLastStepMs_ = 0;
    ledStep_ = 0;

    imagePhase_ = ImagePhase::Intro;
    imagePhaseStartMs_ = millis();
    if (display_) display_->setDirty();
}

void OpenClawEffects::loadSprites() {
    if (spritesLoaded_ || !display_ || !display_->ready()) return;
    spritesLoaded_ = true;

    int w = display_->width();
    int h = display_->height();
    sprite01_ = display_->loadLayerSprite(
        "/avatar/claw_01.png", w, h, ScreenRenderer::kTransparentColor);
    sprite02_ = display_->loadLayerSprite(
        "/avatar/claw_02.png", w, h, ScreenRenderer::kTransparentColor);
    if (!sprite01_.valid() && !sprite02_.valid()) {
        Serial.println("[OpenClaw] image sprites unavailable; image effect disabled");
    }
}

void OpenClawEffects::updateLed() {
    if (!leds_ || !ledActive_) return;

    uint32_t now = millis();
    if (now - ledStartMs_ >= kLedDurationMs) {
        ledActive_ = false;
        leds_->off();
        return;
    }
    if (now - ledLastStepMs_ < kLedStepMs) return;
    ledLastStepMs_ = now;

    uint8_t ledCount = leds_->count();
    if (ledCount < 12) {
        leds_->setColor({168, 0, 0});
        ledStep_++;
        return;
    }

    uint8_t offset = ledStep_++;
    for (uint8_t i = 0; i < 6; ++i) {
        bool red = ((i + offset) % 2) == 0;
        RgbColor color = red ? RgbColor{168, 0, 0} : RgbColor{60, 60, 60};
        leds_->setPixel(i, color);
        leds_->setPixel(6 + i, color);
    }
    leds_->refresh();
}

void OpenClawEffects::updateImage() {
    if (imagePhase_ == ImagePhase::Idle) return;

    uint32_t now = millis();
    uint32_t elapsed = now - imagePhaseStartMs_;
    switch (imagePhase_) {
        case ImagePhase::Intro:
            if (elapsed >= kImageIntroMs) {
                imagePhase_ = ImagePhase::Main;
                imagePhaseStartMs_ = now;
                if (display_) display_->setDirty();
            }
            break;
        case ImagePhase::Main:
            if (elapsed >= kImageMainMs) {
                imagePhase_ = ImagePhase::Outro;
                imagePhaseStartMs_ = now;
                if (display_) display_->setDirty();
            }
            break;
        case ImagePhase::Outro:
            if (elapsed >= kImageOutroMs) {
                imagePhase_ = ImagePhase::Idle;
                if (display_) display_->setDirty();
            }
            break;
        case ImagePhase::Idle:
            break;
    }
}

}  // namespace aiavatar
