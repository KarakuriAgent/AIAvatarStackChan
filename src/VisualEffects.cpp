#include "VisualEffects.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

namespace aiavatar {

namespace {
constexpr int kEffectInset = 8;
constexpr int kGlowWidth = 4;
constexpr int kMaxAlpha = 228;
constexpr uint32_t kFrameIntervalMs = 33;
constexpr float kTwoPi = 6.28318530718f;

bool timeActive(uint32_t untilMs) {
    return static_cast<int32_t>(millis() - untilMs) < 0;
}

void blendPixel(LGFX_Sprite* canvas, int x, int y, int r, int g, int b, int alpha) {
    if (!canvas || alpha <= 0) return;
    if (x < 0 || y < 0 || x >= canvas->width() || y >= canvas->height()) return;
    canvas->drawPixel(x, y, canvas->color565(r, g, b));
}

void drawInsetBorder(LGFX_Sprite* canvas, int red, int green, int blue, int maxAlpha,
                     int inset = kEffectInset, int glowWidth = kGlowWidth) {
    const int w = canvas->width();
    const int h = canvas->height();
    const int bandWidth = inset + glowWidth;

    if (w < bandWidth * 2 || h < bandWidth * 2) return;

    for (int dist = 0; dist < bandWidth; ++dist) {
        int alpha = maxAlpha * (bandWidth - dist) / bandWidth;
        for (int x = dist; x < w - dist; ++x) {
            blendPixel(canvas, x, dist, red, green, blue, alpha);
            blendPixel(canvas, x, h - 1 - dist, red, green, blue, alpha);
        }
        for (int y = dist + 1; y < h - 1 - dist; ++y) {
            blendPixel(canvas, dist, y, red, green, blue, alpha);
            blendPixel(canvas, w - 1 - dist, y, red, green, blue, alpha);
        }
    }
}

void drawPerimeterPixel(LGFX_Sprite* canvas, int pos, int red, int green, int blue, int alpha,
                        int inset, int thickness) {
    const int left = inset;
    const int top = inset;
    const int right = canvas->width() - 1 - inset;
    const int bottom = canvas->height() - 1 - inset;
    const int horizontal = right - left;
    const int vertical = bottom - top;
    const int perimeter = (horizontal + vertical) * 2;
    if (horizontal <= 0 || vertical <= 0 || perimeter <= 0) return;

    pos %= perimeter;
    if (pos < 0) pos += perimeter;

    int x = left;
    int y = top;
    int dx = 0;
    int dy = 0;
    if (pos < horizontal) {
        x = left + pos;
        y = top;
        dy = 1;
    } else if (pos < horizontal + vertical) {
        x = right;
        y = top + (pos - horizontal);
        dx = -1;
    } else if (pos < horizontal * 2 + vertical) {
        x = right - (pos - horizontal - vertical);
        y = bottom;
        dy = -1;
    } else {
        x = left;
        y = bottom - (pos - horizontal * 2 - vertical);
        dx = 1;
    }

    for (int i = 0; i < thickness; ++i) {
        blendPixel(canvas, x + dx * i, y + dy * i, red, green, blue,
                   alpha * (thickness - i) / thickness);
    }
}

void drawMovingPerimeterSegment(LGFX_Sprite* canvas, int red, int green, int blue, int alpha,
                                uint32_t periodMs, int segmentLength, int inset, int thickness) {
    const int horizontal = canvas->width() - 1 - inset * 2;
    const int vertical = canvas->height() - 1 - inset * 2;
    const int perimeter = (horizontal + vertical) * 2;
    if (perimeter <= 0 || periodMs == 0) return;

    int head = static_cast<int>((millis() % periodMs) * perimeter / periodMs);
    for (int i = 0; i < segmentLength; ++i) {
        int falloff = alpha * (segmentLength - i) / segmentLength;
        drawPerimeterPixel(canvas, head - i, red, green, blue, falloff, inset, thickness);
    }
}
}  // namespace

VisualEffects::VisualEffects()
    : voiceDetectedUntilMs_(0),
      acceptedUntilMs_(0),
      toolUntilMs_(0),
      visionUntilMs_(0),
      errorUntilMs_(0),
      processingUntilMs_(0),
      lastFrameMs_(0),
      voiceVisible_(false),
      acceptedVisible_(false),
      toolVisible_(false),
      visionVisible_(false),
      errorVisible_(false),
      processingActive_(false),
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

void VisualEffects::showAccepted(uint32_t durationMs) {
    acceptedUntilMs_ = millis() + durationMs;
    acceptedVisible_ = true;
}

void VisualEffects::showToolPulse(uint32_t durationMs) {
    toolUntilMs_ = millis() + durationMs;
    toolVisible_ = true;
}

void VisualEffects::showVisionFlash(uint32_t durationMs) {
    visionUntilMs_ = millis() + durationMs;
    visionVisible_ = true;
}

void VisualEffects::showErrorFlash(uint32_t durationMs) {
    errorUntilMs_ = millis() + durationMs;
    errorVisible_ = true;
}

void VisualEffects::clearToolPulse() {
    toolUntilMs_ = 0;
    toolVisible_ = false;
}

void VisualEffects::setProcessing(bool processing) {
    processingActive_ = processing;
    processingUntilMs_ = processing ? millis() + 30000 : 0;
    lastFrameMs_ = 0;
}

bool VisualEffects::update() {
    bool dirty = false;

    bool voice = voiceDetected();
    if (voiceVisible_ != voice) {
        voiceVisible_ = voice;
        dirty = true;
    }

    bool accepted = acceptedActive();
    if (acceptedVisible_ != accepted) {
        acceptedVisible_ = accepted;
        dirty = true;
    }

    bool tool = toolActive();
    if (toolVisible_ != tool) {
        toolVisible_ = tool;
        dirty = true;
    }

    bool vision = visionActive();
    if (visionVisible_ != vision) {
        visionVisible_ = vision;
        dirty = true;
    }

    bool error = errorActive();
    if (errorVisible_ != error) {
        errorVisible_ = error;
        dirty = true;
    }

    bool processing = processingVisible();
    if (processingActive_ != processing) {
        processingActive_ = processing;
        dirty = true;
    }

    if (anyEffectActive()) {
        uint32_t now = millis();
        if (lastFrameMs_ == 0 || now - lastFrameMs_ >= kFrameIntervalMs) {
            lastFrameMs_ = now;
            dirty = true;
        }
    }

    return dirty;
}

void VisualEffects::draw(LGFX_Sprite* canvas) const {
    if (!canvas || !anyEffectActive()) return;

    if (processingActive_) drawProcessingPulse(canvas);
    if (toolVisible_) drawToolPulse(canvas);
    if (visionVisible_) drawVisionFlash(canvas);
    if (acceptedVisible_) drawAcceptedFlash(canvas);
    if (voiceVisible_) {
        if (glowShape_ == ListeningGlowShape::Circle) {
            drawCircularListeningBorder(canvas);
        } else {
            drawListeningBorder(canvas);
        }
    }
    if (errorVisible_) drawErrorFlash(canvas);
}

bool VisualEffects::voiceDetected() const {
    return timeActive(voiceDetectedUntilMs_);
}

bool VisualEffects::acceptedActive() const {
    return timeActive(acceptedUntilMs_);
}

bool VisualEffects::toolActive() const {
    return timeActive(toolUntilMs_);
}

bool VisualEffects::visionActive() const {
    return timeActive(visionUntilMs_);
}

bool VisualEffects::errorActive() const {
    return timeActive(errorUntilMs_);
}

bool VisualEffects::processingVisible() const {
    return processingActive_ && timeActive(processingUntilMs_);
}

bool VisualEffects::anyEffectActive() const {
    return voiceVisible_ || acceptedVisible_ || toolVisible_ || visionVisible_ || errorVisible_ ||
           processingVisible();
}

void VisualEffects::drawListeningBorder(LGFX_Sprite* canvas) const {
    const int w = canvas->width();
    const int h = canvas->height();
    const int bandWidth = kEffectInset + kGlowWidth;

    if (w < bandWidth * 2 || h < bandWidth * 2) return;

    const int cA_r = 150, cA_g = 50, cA_b = 255;
    const int cB_r = 255, cB_g = 40, cB_b = 180;

    auto drawGradientPixel = [&](int x, int y, int alpha) {
        int t = ((w - 1 - x) * 128 / (w - 1)) + (y * 128 / (h - 1));
        int gr = cA_r + (cB_r - cA_r) * t / 256;
        int gg = cA_g + (cB_g - cA_g) * t / 256;
        int gb = cA_b + (cB_b - cA_b) * t / 256;
        blendPixel(canvas, x, y, gr, gg, gb, alpha);
    };

    for (int dist = 0; dist < bandWidth; ++dist) {
        int alpha = kMaxAlpha * (bandWidth - dist) / bandWidth;
        for (int x = dist; x < w - dist; ++x) {
            drawGradientPixel(x, dist, alpha);
            drawGradientPixel(x, h - 1 - dist, alpha);
        }
        for (int y = dist + 1; y < h - 1 - dist; ++y) {
            drawGradientPixel(dist, y, alpha);
            drawGradientPixel(w - 1 - dist, y, alpha);
        }
    }
}

void VisualEffects::drawAcceptedFlash(LGFX_Sprite* canvas) const {
    uint32_t remaining = acceptedUntilMs_ - millis();
    int alpha = 48 + static_cast<int>(180 * (remaining > 1000 ? 1000 : remaining) / 1000);
    drawInsetBorder(canvas, 0, 190, 80, alpha);
}

void VisualEffects::drawToolPulse(LGFX_Sprite* canvas) const {
    drawProcessingPulse(canvas);
}

void VisualEffects::drawVisionFlash(LGFX_Sprite* canvas) const {
    bool on = (millis() % 210) < 80;
    if (!on) return;
    drawInsetBorder(canvas, 0, 100, 255, 210);
    drawMovingPerimeterSegment(canvas, 120, 210, 255, 240, 420, 80, kEffectInset, 3);
}

void VisualEffects::drawErrorFlash(LGFX_Sprite* canvas) const {
    uint32_t remaining = errorUntilMs_ - millis();
    uint32_t clamped = remaining > 900 ? 900 : remaining;
    int alpha = 80 + static_cast<int>(175 * clamped / 900);
    bool on = (millis() % 180) < 120;
    if (on) drawInsetBorder(canvas, 230, 30, 40, alpha);
}

void VisualEffects::drawProcessingPulse(LGFX_Sprite* canvas) const {
    float phase = static_cast<float>(millis() % 1400) / 1400.0f;
    float brightness = (sinf(phase * kTwoPi - kTwoPi * 0.25f) + 1.0f) * 0.5f;
    int alpha = 44 + static_cast<int>(80 * brightness);
    drawInsetBorder(canvas, 40, 190, 210, alpha);
    drawMovingPerimeterSegment(canvas, 80, 230, 220, 185, 1800, 64, kEffectInset, 2);
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
