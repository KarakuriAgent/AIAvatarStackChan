#include "StatusOverlay.h"

#include <Arduino.h>
#include <cmath>

namespace aiavatar {

StatusOverlay::StatusOverlay()
    : enabled_(true),
      hasState_(false),
      state_{},
      layout_{18, 6, top_left, 3,
              {224, 0, 37, 37},
              {254, 0, 37, 37},
              {288, 4, 28, 28},
              28,
              0,
              {0, 190, 72, 50},
              5},
      micTapBounds_(layout_.micBounds),
      networkTapBounds_(layout_.networkBounds),
      batteryTapBounds_(layout_.batteryBounds),
      customMicTapBounds_(false),
      customNetworkTapBounds_(false),
      customBatteryTapBounds_(false) {}

bool StatusOverlay::update(const StatusOverlayState& state) {
    if (hasState_ && equals(state_, state)) return false;
    state_ = state;
    hasState_ = true;
    return enabled_;
}

void StatusOverlay::draw(LGFX_Sprite* canvas) const {
    if (!enabled_ || !hasState_ || !canvas) return;

    drawClock(canvas, state_.hour, state_.minute);
    drawMicIcon(canvas, state_.micMuted);
    drawWiFiIcon(canvas, state_.wifiConnected, state_.websocketConnected);
    drawBatteryIcon(canvas, state_.batteryLevel, state_.batteryCharging);
    if (state_.volumeVisible) {
        drawVolumeIndicator(canvas, state_.volumeLevel, state_.volumeLevelCount);
    }
}

bool StatusOverlay::equals(const StatusOverlayState& a, const StatusOverlayState& b) {
    return a.micMuted == b.micMuted &&
           a.volumeVisible == b.volumeVisible &&
           a.volumeLevel == b.volumeLevel &&
           a.volumeLevelCount == b.volumeLevelCount &&
           a.wifiConnected == b.wifiConnected &&
           a.websocketConnected == b.websocketConnected &&
           a.batteryLevel == b.batteryLevel &&
           a.batteryCharging == b.batteryCharging &&
           a.hour == b.hour &&
           a.minute == b.minute;
}

void StatusOverlay::setClockPosition(int16_t x, int16_t y, textdatum_t datum) {
    layout_.clockX = x;
    layout_.clockY = y;
    layout_.clockDatum = datum;
}

void StatusOverlay::setMicBounds(UiRect bounds) {
    layout_.micBounds = bounds;
    if (!customMicTapBounds_) micTapBounds_ = bounds;
}

void StatusOverlay::setNetworkBounds(UiRect bounds) {
    layout_.networkBounds = bounds;
    if (!customNetworkTapBounds_) networkTapBounds_ = bounds;
}

void StatusOverlay::setBatteryBounds(UiRect bounds) {
    layout_.batteryBounds = bounds;
    if (!customBatteryTapBounds_) batteryTapBounds_ = bounds;
}

void StatusOverlay::setMicTapBounds(UiRect bounds) {
    micTapBounds_ = bounds;
    customMicTapBounds_ = true;
}

void StatusOverlay::setNetworkTapBounds(UiRect bounds) {
    networkTapBounds_ = bounds;
    customNetworkTapBounds_ = true;
}

void StatusOverlay::setBatteryTapBounds(UiRect bounds) {
    batteryTapBounds_ = bounds;
    customBatteryTapBounds_ = true;
}

void StatusOverlay::drawClock(LGFX_Sprite* canvas, uint8_t hour, uint8_t minute) const {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", hour, minute);

    const int x = layout_.clockX;
    const int y = layout_.clockY;
    canvas->setFont(nullptr);
    canvas->setTextSize(layout_.clockTextSize);
    canvas->setTextDatum(layout_.clockDatum);
    canvas->setTextColor(TFT_BLACK);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            canvas->drawString(buf, x + dx, y + dy);
        }
    }
    canvas->setTextColor(TFT_WHITE);
    canvas->drawString(buf, x, y);
}

void StatusOverlay::drawMicIcon(LGFX_Sprite* canvas, bool muted) const {
    UiRect bounds = layout_.micBounds;
    const int s = layout_.iconSize;
    const int x = bounds.x + (bounds.w - s) / 2;
    const int y = bounds.y + (bounds.h - s) / 2;
    const int cx = x + s / 2;
    const int cy = y + s * 3 / 7;

    canvas->fillRoundRect(x, y, s, s, std::max(1, s * 3 / 14), 0x2104);
    uint16_t color = muted ? TFT_WHITE : TFT_GREEN;
    canvas->fillRoundRect(cx - s * 3 / 28, cy - s / 4, s / 4, s * 11 / 28,
                          std::max(1, s * 3 / 28), color);
    canvas->drawRoundRect(cx - s * 3 / 14, cy - s / 14, s * 13 / 28, s * 9 / 28,
                          std::max(1, s / 7), color);
    canvas->drawFastVLine(cx, cy + s / 4, std::max(1, s * 3 / 28), color);
    canvas->drawFastHLine(cx - s * 3 / 28, cy + s * 5 / 14, s / 4, color);
    if (muted) {
        canvas->drawLine(x + s / 7, y + s * 6 / 7, x + s * 6 / 7, y + s / 7, TFT_WHITE);
        canvas->drawLine(x + s / 7 + 1, y + s * 6 / 7, x + s * 6 / 7 + 1, y + s / 7, TFT_WHITE);
    }
}

void StatusOverlay::drawWiFiIcon(LGFX_Sprite* canvas, bool wifiConnected, bool wsConnected) const {
    UiRect bounds = layout_.networkBounds;
    const int s = layout_.iconSize;
    const int x = bounds.x + (bounds.w - s) / 2;
    const int y = bounds.y + (bounds.h - s) / 2;
    const int cx = x + s / 2;
    const int by = y + s * 19 / 28;

    canvas->fillRoundRect(x, y, s, s, std::max(1, s * 3 / 14), 0x2104);
    uint16_t color;
    if (!wifiConnected) color = TFT_RED;
    else if (wsConnected) color = TFT_GREEN;
    else color = TFT_WHITE;

    const int stroke = layout_.wifiStrokeRadius;
    for (int a = -50; a <= 50; ++a) {
        float rad = a * 3.14159f / 180.0f;
        int px = cx + static_cast<int>(s * 11 / 28 * sinf(rad));
        int py = by - static_cast<int>(s * 11 / 28 * cosf(rad));
        if (stroke > 0) canvas->fillCircle(px, py, stroke, color);
        else canvas->drawPixel(px, py, color);
    }
    for (int a = -50; a <= 50; ++a) {
        float rad = a * 3.14159f / 180.0f;
        int px = cx + static_cast<int>(s / 4 * sinf(rad));
        int py = by - static_cast<int>(s / 4 * cosf(rad));
        if (stroke > 0) canvas->fillCircle(px, py, stroke, color);
        else canvas->drawPixel(px, py, color);
    }
    for (int a = -50; a <= 50; ++a) {
        float rad = a * 3.14159f / 180.0f;
        int px = cx + static_cast<int>(s * 3 / 28 * sinf(rad));
        int py = by - static_cast<int>(s * 3 / 28 * cosf(rad));
        if (stroke > 0) canvas->fillCircle(px, py, stroke, color);
        else canvas->drawPixel(px, py, color);
    }
    canvas->fillCircle(cx, by, std::max(1, s / 28), color);
    if (!wifiConnected) {
        canvas->drawLine(x + s / 7, y + s * 6 / 7, x + s * 6 / 7, y + s / 7, TFT_RED);
        canvas->drawLine(x + s / 7 + 1, y + s * 6 / 7, x + s * 6 / 7 + 1, y + s / 7, TFT_RED);
    }
}

void StatusOverlay::drawBatteryIcon(LGFX_Sprite* canvas, int8_t level, bool charging) const {
    UiRect bounds = layout_.batteryBounds;
    const int s = layout_.iconSize;
    const int x = bounds.x + (bounds.w - s) / 2;
    const int y = bounds.y + (bounds.h - s) / 2;
    canvas->fillRoundRect(x, y, s, s, std::max(1, s * 3 / 14), 0x2104);

    const uint16_t outline = 0xC618;
    canvas->drawRoundRect(x + s * 3 / 28, y + s * 2 / 7, s * 9 / 14, s * 3 / 7,
                          std::max(1, s / 14), outline);
    canvas->fillRect(x + s * 3 / 4, y + s * 11 / 28, std::max(1, s * 3 / 28),
                     s * 3 / 14, outline);

    if (level < 0) {
        canvas->setTextColor(outline);
        canvas->setTextSize(1);
        canvas->drawChar('?', x + s / 3, y + s * 5 / 14);
        return;
    }

    uint16_t fillColor;
    if (level >= 50) fillColor = TFT_GREEN;
    else if (level >= 20) fillColor = TFT_YELLOW;
    else fillColor = TFT_RED;

    int fillW = static_cast<int>(level) * (s / 2) / 100;
    if (fillW < 1 && level > 0) fillW = 1;
    if (fillW > 0) canvas->fillRect(x + s * 5 / 28, y + s * 5 / 14,
                                     fillW, s * 2 / 7, fillColor);

    if (charging) {
        const uint16_t boltColor = TFT_WHITE;
        int cx = x + s * 3 / 7;
        int cy = y + s / 2;
        canvas->fillTriangle(cx + s * 3 / 28, cy - s * 3 / 14, cx - s * 3 / 28,
                             cy, cx + s / 28, cy, boltColor);
        canvas->fillTriangle(cx - s / 28, cy, cx + s * 3 / 28, cy,
                             cx - s * 3 / 28, cy + s * 3 / 14, boltColor);
    }
}

void StatusOverlay::drawVolumeIndicator(LGFX_Sprite* canvas, uint8_t level,
                                        uint8_t levelCount) const {
    if (!canvas || levelCount <= 1) return;

    const uint8_t n = levelCount - 1;
    const int barH = 28;
    const int gap = 5;
    const int maxW = 30;
    const int x = layout_.volumeIndicatorX;
    const int totalH = n * barH + (n - 1) * gap;
    const int startY = (canvas->height() - totalH) / 2;

    canvas->fillRoundRect(x - 3, startY - 6, maxW + 6, totalH + 12, 6, 0x2104);
    for (uint8_t i = 1; i <= n; ++i) {
        int fromTop = n - i;
        int y = startY + fromTop * (barH + gap);
        int w = maxW * i / n;
        bool filled = level >= i;
        canvas->fillRoundRect(x, y, w, barH, 3, filled ? TFT_GREEN : 0x4208);
    }
}

}  // namespace aiavatar
