#pragma once

#include <M5Unified.h>
#include <cstdint>

namespace aiavatar {

class VisualEffects {
public:
    VisualEffects();

    void showVoiceDetected(uint32_t durationMs);
    void showAccepted(uint32_t durationMs = 1000);
    void showToolPulse(uint32_t durationMs = 30000);
    void showVisionFlash(uint32_t durationMs = 420);
    void showErrorFlash(uint32_t durationMs = 900);
    void clearToolPulse();
    void setProcessing(bool processing);
    bool update();
    void draw(LGFX_Sprite* canvas) const;
    bool voiceDetected() const;

private:
    uint32_t voiceDetectedUntilMs_;
    uint32_t acceptedUntilMs_;
    uint32_t toolUntilMs_;
    uint32_t visionUntilMs_;
    uint32_t errorUntilMs_;
    uint32_t processingUntilMs_;
    uint32_t lastFrameMs_;
    bool voiceVisible_;
    bool acceptedVisible_;
    bool toolVisible_;
    bool visionVisible_;
    bool errorVisible_;
    bool processingActive_;

    bool acceptedActive() const;
    bool toolActive() const;
    bool visionActive() const;
    bool errorActive() const;
    bool processingVisible() const;
    bool anyEffectActive() const;
    void drawListeningBorder(LGFX_Sprite* canvas) const;
    void drawAcceptedFlash(LGFX_Sprite* canvas) const;
    void drawToolPulse(LGFX_Sprite* canvas) const;
    void drawVisionFlash(LGFX_Sprite* canvas) const;
    void drawErrorFlash(LGFX_Sprite* canvas) const;
    void drawProcessingPulse(LGFX_Sprite* canvas) const;
};

}  // namespace aiavatar
