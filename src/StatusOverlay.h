#pragma once

#include <M5Unified.h>
#include <cstdint>

namespace aiavatar {

struct UiRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;

    bool contains(int16_t px, int16_t py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct StatusOverlayState {
    bool micMuted;
    bool volumeVisible;
    uint8_t volumeLevel;
    uint8_t volumeLevelCount;
    bool wifiConnected;
    bool websocketConnected;
    int8_t batteryLevel;
    bool batteryCharging;
    uint8_t hour;
    uint8_t minute;
};

class StatusOverlay {
public:
    StatusOverlay();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    bool update(const StatusOverlayState& state);
    void draw(LGFX_Sprite* canvas) const;
    UiRect micBounds() const { return {246, 8, 37, 37}; }
    UiRect networkBounds() const { return {276, 8, 37, 37}; }
    UiRect volumeTapBounds() const { return {0, 190, 72, 50}; }

private:
    bool enabled_;
    bool hasState_;
    StatusOverlayState state_;

    static bool equals(const StatusOverlayState& a, const StatusOverlayState& b);
    static void drawClock(LGFX_Sprite* canvas, uint8_t hour, uint8_t minute);
    static void drawWiFiIcon(LGFX_Sprite* canvas, bool wifiConnected, bool wsConnected);
    static void drawMicIcon(LGFX_Sprite* canvas, bool muted);
    static void drawVolumeIndicator(LGFX_Sprite* canvas, uint8_t level, uint8_t levelCount);
};

}  // namespace aiavatar
