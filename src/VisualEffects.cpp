#include "VisualEffects.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

namespace aiavatar {

VisualEffects::VisualEffects()
    : voiceDetectedUntilMs_(0),
      voiceVisible_(false),
      glowShape_(ListeningGlowShape::Rectangle),
      circularGlowWidth_(7.0f),
      seamlessCircularGlowGradient_(false) {}

bool VisualEffects::showVoiceDetected(uint32_t durationMs) {
    bool wasVisible = voiceDetected();
    voiceDetectedUntilMs_ = millis() + durationMs;
    voiceVisible_ = true;
    return !wasVisible;
}

bool VisualEffects::clearVoiceDetected() {
    bool wasVisible = voiceVisible_;
    voiceDetectedUntilMs_ = 0;
    voiceVisible_ = false;
    return wasVisible;
}

bool VisualEffects::update() {
    bool visible = voiceDetected();
    if (voiceVisible_ == visible) return false;
    voiceVisible_ = visible;
    return true;
}

void VisualEffects::draw(LGFX_Sprite* canvas) const {
    if (!canvas || !voiceVisible_) return;
    if (glowShape_ == ListeningGlowShape::Circle) {
        drawCircularListeningBorder(canvas);
    } else {
        drawListeningBorder(canvas);
    }
}

bool VisualEffects::voiceDetected() const {
    return static_cast<int32_t>(millis() - voiceDetectedUntilMs_) < 0;
}

void VisualEffects::drawListeningBorder(LGFX_Sprite* canvas) const {
    const int w = canvas->width();
    const int h = canvas->height();
    const int glowWidth = 4;
    const int maxAlpha = 228;

    const int cA_r = 150, cA_g = 50, cA_b = 255;
    const int cB_r = 255, cB_g = 40, cB_b = 180;

    auto drawGlowPixel = [&](int x, int y, int dist) {
        int alpha = maxAlpha * (glowWidth - dist) / glowWidth;
        int t = ((w - 1 - x) * 128 / (w - 1)) + (y * 128 / (h - 1));

        int gr = cA_r + (cB_r - cA_r) * t / 256;
        int gg = cA_g + (cB_g - cA_g) * t / 256;
        int gb = cA_b + (cB_b - cA_b) * t / 256;

        uint32_t raw = canvas->readPixelValue(x, y);
        int bgR = ((raw >> 11) & 0x1F) << 3;
        int bgG = ((raw >> 5) & 0x3F) << 2;
        int bgB = (raw & 0x1F) << 3;

        int outR = bgR + (gr - bgR) * alpha / 255;
        int outG = bgG + (gg - bgG) * alpha / 255;
        int outB = bgB + (gb - bgB) * alpha / 255;

        canvas->drawPixel(x, y, canvas->color565(outR, outG, outB));
    };

    for (int dist = 0; dist < glowWidth; ++dist) {
        for (int x = dist; x < w - dist; ++x) {
            drawGlowPixel(x, dist, dist);
            drawGlowPixel(x, h - 1 - dist, dist);
        }
        for (int y = dist + 1; y < h - 1 - dist; ++y) {
            drawGlowPixel(dist, y, dist);
            drawGlowPixel(w - 1 - dist, y, dist);
        }
    }
}

void VisualEffects::drawCircularListeningBorder(LGFX_Sprite* canvas) const {
    const int w = canvas->width();
    const int h = canvas->height();
    const float glowWidth = circularGlowWidth_;
    const float cx = (w - 1) * 0.5f;
    const float cy = (h - 1) * 0.5f;
    const float radius = std::min(w, h) * 0.5f - 0.5f;
    const float inner = radius - glowWidth;

    const int cA_r = 150, cA_g = 50, cA_b = 255;
    const int cB_r = 255, cB_g = 40, cB_b = 180;

    auto drawGlowPixel = [&](int x, int y, int alpha) {
        int t;
        if (seamlessCircularGlowGradient_) {
            float dx = (w > 1) ? static_cast<float>(x) / static_cast<float>(w - 1) : 0.5f;
            float dy = (h > 1) ? static_cast<float>(y) / static_cast<float>(h - 1) : 0.5f;
            t = static_cast<int>((1.0f - dx) * 128.0f + dy * 127.0f);
        } else {
            float angle = atan2f(y - cy, x - cx) + 3.14159f;
            t = static_cast<int>(angle * 255.0f / (2.0f * 3.14159f));
        }

        int gr = cA_r + (cB_r - cA_r) * t / 255;
        int gg = cA_g + (cB_g - cA_g) * t / 255;
        int gb = cA_b + (cB_b - cA_b) * t / 255;

        int outR = gr * alpha / 255;
        int outG = gg * alpha / 255;
        int outB = gb * alpha / 255;

        canvas->drawPixel(x, y, canvas->color565(outR, outG, outB));
    };

    constexpr float kSampleOffset[2] = {-0.25f, 0.25f};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float alphaSum = 0.0f;
            for (float oy : kSampleOffset) {
                float sy = y + oy - cy;
                for (float ox : kSampleOffset) {
                    float sx = x + ox - cx;
                    float dist = sqrtf(sx * sx + sy * sy);
                    if (dist < inner || dist > radius + 0.5f) continue;

                    float edgeCoverage = radius + 0.5f - dist;
                    if (edgeCoverage > 1.0f) edgeCoverage = 1.0f;
                    if (edgeCoverage < 0.0f) edgeCoverage = 0.0f;

                    float glow = (dist - inner) / glowWidth;
                    if (glow < 0.0f) glow = 0.0f;
                    if (glow > 1.0f) glow = 1.0f;
                    alphaSum += 255.0f * sqrtf(glow) * edgeCoverage;
                }
            }
            int alpha = static_cast<int>(alphaSum * 0.25f);
            if (alpha > 0) drawGlowPixel(x, y, alpha);
        }
    }
}

}  // namespace aiavatar
