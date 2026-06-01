#pragma once

#include "ScreenRenderer.h"

#include <cstdint>

namespace aiavatar {

enum class Expression : uint8_t {
    Neutral = 0,
    Joy,
    Fun,
    Angry,
    Sorrow,
    Surprised,
    Count,
};

enum class MouthShape : uint8_t {
    None = 0,
    Half,
    Open,
    Count,
};

class FaceController {
public:
    FaceController();

    bool begin(ScreenRenderer& display);
    void update(bool speakerPlaying, float audioRms);
    void setExpression(Expression expression, uint32_t durationMs = 0);
    void setExpression(const char* faceName, float durationSec = 0.0f);

    static Expression parseExpression(const char* faceName);

private:
    ScreenRenderer* display_;
    LGFX_Sprite* faceSprites_[static_cast<uint8_t>(Expression::Count)];
    SpriteLayer mouthSprites_[static_cast<uint8_t>(MouthShape::Count)];
    SpriteLayer blinkSprite_;

    volatile Expression currentExpression_;
    MouthShape currentMouth_;
    bool blinking_;
    uint32_t expressionEndMs_;
    uint32_t nextBlinkMs_;
    uint32_t blinkEndMs_;
    bool started_;

    void loadSprites();
    void scheduleBlink(uint32_t now);
    void applyToDisplay();
};

}  // namespace aiavatar
