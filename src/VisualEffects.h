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
    void showAccepted(uint32_t durationMs = 1000);
    void showToolPulse(uint32_t durationMs = 30000);
    void showVisionFlash(uint32_t durationMs = 420);
    void showErrorFlash(uint32_t durationMs = 900);
    void clearToolPulse();
    void setProcessing(bool processing);
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
    uint32_t acceptedUntilMs_;
    uint32_t toolUntilMs_;
    uint32_t visionUntilMs_;
    uint32_t errorUntilMs_;
    uint32_t lastFrameMs_;
    bool voiceVisible_;
    bool acceptedVisible_;
    bool toolVisible_;
    bool visionVisible_;
    bool errorVisible_;
    bool processingActive_;
    ListeningGlowShape glowShape_;
    float circularGlowWidth_;
    bool seamlessCircularGlowGradient_;

    bool acceptedActive() const;
    bool toolActive() const;
    bool visionActive() const;
    bool errorActive() const;
    bool anyEffectActive() const;
    void drawListeningBorder(LGFX_Sprite* canvas) const;
    void drawCircularListeningBorder(LGFX_Sprite* canvas) const;
    void drawAcceptedFlash(LGFX_Sprite* canvas) const;
    void drawToolPulse(LGFX_Sprite* canvas) const;
    void drawVisionFlash(LGFX_Sprite* canvas) const;
    void drawErrorFlash(LGFX_Sprite* canvas) const;
    void drawProcessingPulse(LGFX_Sprite* canvas) const;
};

}  // namespace aiavatar
