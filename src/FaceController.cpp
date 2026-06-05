#include "FaceController.h"

#include <Arduino.h>
#include <cstring>
#include <strings.h>

namespace aiavatar {

static constexpr uint32_t kBlinkMinMs = 3000;
static constexpr uint32_t kBlinkMaxMs = 8000;
static constexpr uint32_t kBlinkDurationMs = 120;
static constexpr float kLipsyncHalfThreshold = 0.025f;
static constexpr float kLipsyncOpenThreshold = 0.075f;

static const char* kFacePaths[] = {
    "/avatar/neutral.png",
    "/avatar/joy.png",
    "/avatar/fun.png",
    "/avatar/angry.png",
    "/avatar/sorrow.png",
    "/avatar/surprised.png",
};

static const char* kMouthPaths[] = {
    nullptr,
    "/avatar/mouth_half.png",
    "/avatar/mouth_open.png",
};

FaceController::FaceController()
    : display_(nullptr),
      blinkSprite_{nullptr, 0, 0, ScreenRenderer::kTransparentColor},
      currentExpression_(Expression::Neutral),
      currentMouth_(MouthShape::None),
      blinking_(false),
      expressionEndMs_(0),
      nextBlinkMs_(0),
      blinkEndMs_(0),
      lazyLoadNextMs_(0),
      lazyLoadStep_(0),
      deferredLoadingEnabled_(true),
      started_(false) {
    for (auto& sprite : faceSprites_) sprite = nullptr;
    for (auto& sprite : mouthSprites_) {
        sprite = {nullptr, 0, 0, ScreenRenderer::kTransparentColor};
    }
}

bool FaceController::begin(ScreenRenderer& display) {
    display_ = &display;
    loadInitialSprites();
    scheduleBlink(millis());
    started_ = faceSprites_[static_cast<uint8_t>(Expression::Neutral)] != nullptr;
    while (started_ && lazyLoadStep_ < 2 + static_cast<uint8_t>(Expression::Count)) {
        loadNextDeferredSprite(true);
    }
    applyToDisplay();
    return started_;
}

bool FaceController::beginMinimal(ScreenRenderer& display) {
    display_ = &display;
    loadInitialSprites();
    scheduleBlink(millis());
    started_ = faceSprites_[static_cast<uint8_t>(Expression::Neutral)] != nullptr;
    applyToDisplay();
    return started_;
}

void FaceController::loadInitialSprites() {
    if (!display_) return;
    int w = display_->width();
    int h = display_->height();

    faceSprites_[static_cast<uint8_t>(Expression::Neutral)] =
        display_->loadSprite(kFacePaths[static_cast<uint8_t>(Expression::Neutral)], w, h);
    if (!faceSprites_[static_cast<uint8_t>(Expression::Neutral)]) {
        Serial.println("[Face] neutral image missing; using black fallback");
        auto* sprite = new LGFX_Sprite(&M5.Display);
        if (sprite) {
            sprite->setColorDepth(16);
            sprite->setPsram(true);
            if (sprite->createSprite(w, h)) {
                sprite->fillSprite(TFT_BLACK);
                faceSprites_[static_cast<uint8_t>(Expression::Neutral)] = sprite;
            } else {
                delete sprite;
            }
        }
    }

    lazyLoadStep_ = 0;
    lazyLoadNextMs_ = millis() + 1000;
}

bool FaceController::loadNextDeferredSprite(bool ignoreSchedule) {
    if (!display_ || !started_) return true;
    if (lazyLoadStep_ >= 2 + static_cast<uint8_t>(Expression::Count)) return true;
    uint32_t now = millis();
    if (!ignoreSchedule && static_cast<int32_t>(now - lazyLoadNextMs_) < 0) return false;

    int w = display_->width();
    int h = display_->height();
    switch (lazyLoadStep_) {
        case 0:
            blinkSprite_ =
                display_->loadLayerSprite("/avatar/neutral_blink.png", w, h,
                                          ScreenRenderer::kTransparentColor);
            if (!blinkSprite_.valid()) Serial.println("[Face] blink image missing; blink disabled");
            break;
        case 1:
        case 2: {
            uint8_t mouthIndex = lazyLoadStep_;
            mouthSprites_[mouthIndex] =
                display_->loadLayerSprite(kMouthPaths[mouthIndex], w, h,
                                          ScreenRenderer::kTransparentColor);
            if (!mouthSprites_[mouthIndex].valid()) {
                Serial.printf("[Face] missing %s; lipsync shape disabled\n",
                              kMouthPaths[mouthIndex]);
            }
            if (static_cast<uint8_t>(currentMouth_) == mouthIndex) applyToDisplay();
            break;
        }
        default: {
            uint8_t faceIndex = lazyLoadStep_ - 2;
            if (faceIndex < static_cast<uint8_t>(Expression::Count)) {
                faceSprites_[faceIndex] = display_->loadSprite(kFacePaths[faceIndex], w, h);
                if (!faceSprites_[faceIndex]) {
                    Serial.printf("[Face] missing %s; using neutral\n", kFacePaths[faceIndex]);
                }
                if (static_cast<uint8_t>(currentExpression_) == faceIndex) applyToDisplay();
            }
            break;
        }
    }
    ++lazyLoadStep_;
    lazyLoadNextMs_ = now + 120;
    return true;
}

void FaceController::update(bool speakerPlaying, float audioRms) {
    if (!started_ || !display_) return;
    if (deferredLoadingEnabled_) loadNextDeferredSprite();

    uint32_t now = millis();
    bool changed = false;

    if (expressionEndMs_ > 0 && static_cast<int32_t>(now - expressionEndMs_) >= 0) {
        expressionEndMs_ = 0;
        currentExpression_ = Expression::Neutral;
        blinking_ = false;
        blinkEndMs_ = 0;
        scheduleBlink(now);
        changed = true;
    }

    if (currentExpression_ == Expression::Neutral && blinkSprite_.valid()) {
        if (blinkEndMs_ > 0 && static_cast<int32_t>(now - blinkEndMs_) >= 0) {
            blinkEndMs_ = 0;
            blinking_ = false;
            scheduleBlink(now);
            changed = true;
        } else if (blinkEndMs_ == 0 && static_cast<int32_t>(now - nextBlinkMs_) >= 0) {
            blinking_ = true;
            blinkEndMs_ = now + kBlinkDurationMs;
            changed = true;
        }
    }

    MouthShape mouth = MouthShape::None;
    if (speakerPlaying) {
        if (audioRms >= kLipsyncOpenThreshold) {
            mouth = MouthShape::Open;
        } else if (audioRms >= kLipsyncHalfThreshold) {
            mouth = MouthShape::Half;
        }
    }
    if (currentMouth_ != mouth) {
        currentMouth_ = mouth;
        changed = true;
    }

    if (changed) applyToDisplay();
}

void FaceController::setExpression(Expression expression, uint32_t durationMs) {
    if (!started_) return;
    if (expression >= Expression::Count) expression = Expression::Neutral;

    bool changed = currentExpression_ != expression;
    currentExpression_ = expression;
    expressionEndMs_ = durationMs > 0 ? millis() + durationMs : 0;
    if (expression != Expression::Neutral) {
        blinking_ = false;
        blinkEndMs_ = 0;
    }
    if (changed) applyToDisplay();
}

void FaceController::setExpression(const char* faceName, float durationSec) {
    uint32_t durationMs = durationSec > 0.0f ? static_cast<uint32_t>(durationSec * 1000.0f) : 0;
    setExpression(parseExpression(faceName), durationMs);
}

Expression FaceController::parseExpression(const char* faceName) {
    if (!faceName || !faceName[0]) return Expression::Neutral;
    if (strcasecmp(faceName, "joy") == 0 || strcasecmp(faceName, "happy") == 0) {
        return Expression::Joy;
    }
    if (strcasecmp(faceName, "fun") == 0) return Expression::Fun;
    if (strcasecmp(faceName, "angry") == 0) return Expression::Angry;
    if (strcasecmp(faceName, "sorrow") == 0 || strcasecmp(faceName, "sad") == 0) {
        return Expression::Sorrow;
    }
    if (strcasecmp(faceName, "surprised") == 0 || strcasecmp(faceName, "surprise") == 0) {
        return Expression::Surprised;
    }
    return Expression::Neutral;
}

void FaceController::scheduleBlink(uint32_t now) {
    uint32_t span = kBlinkMaxMs - kBlinkMinMs;
    nextBlinkMs_ = now + kBlinkMinMs + (span > 0 ? esp_random() % span : 0);
}

void FaceController::applyToDisplay() {
    if (!display_) return;
    LGFX_Sprite* base = faceSprites_[static_cast<uint8_t>(currentExpression_)];
    if (!base) base = faceSprites_[static_cast<uint8_t>(Expression::Neutral)];

    SpriteLayer mouth{nullptr, 0, 0, ScreenRenderer::kTransparentColor};
    if (currentMouth_ != MouthShape::None) {
        mouth = mouthSprites_[static_cast<uint8_t>(currentMouth_)];
    }

    SpriteLayer blink{nullptr, 0, 0, ScreenRenderer::kTransparentColor};
    if (blinking_ && currentExpression_ == Expression::Neutral) {
        blink = blinkSprite_;
    }

    display_->setBase(base);
    display_->setOverlay(mouth);
    display_->setOverlay2(blink);
}

}  // namespace aiavatar
