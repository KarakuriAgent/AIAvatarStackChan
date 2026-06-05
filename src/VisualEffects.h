#pragma once

#include <M5Unified.h>
#include <cstdint>

namespace aiavatar {

enum class ListeningGlowShape : uint8_t {
    Rectangle = 0,
    Circle,
};

class VisualEffects {
public:
    VisualEffects();

    bool showVoiceDetected(uint32_t durationMs);
    bool clearVoiceDetected();
    bool update();
    void draw(LGFX_Sprite* canvas) const;
    bool voiceDetected() const;
    void setListeningGlowShape(ListeningGlowShape shape) { glowShape_ = shape; }
    ListeningGlowShape listeningGlowShape() const { return glowShape_; }
    void setCircularListeningGlowWidth(float width) { circularGlowWidth_ = width; }
    void setCircularListeningGlowSeamlessGradient(bool enabled) {
        seamlessCircularGlowGradient_ = enabled;
    }

private:
    uint32_t voiceDetectedUntilMs_;
    bool voiceVisible_;
    ListeningGlowShape glowShape_;
    float circularGlowWidth_;
    bool seamlessCircularGlowGradient_;

    void drawListeningBorder(LGFX_Sprite* canvas) const;
    void drawCircularListeningBorder(LGFX_Sprite* canvas) const;
};

}  // namespace aiavatar
