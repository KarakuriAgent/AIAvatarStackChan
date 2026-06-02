#pragma once

#include "Config.h"
#include "StatusOverlay.h"

#include <M5Unified.h>
#include <cstdint>

namespace aiavatar {

class AIAvatar;

using TapCallback = bool (*)(int16_t x, int16_t y);

enum class ButtonId : uint8_t {
    A = 0,
    B,
    C,
    Count,
};

enum class ButtonAction : uint8_t {
    None = 0,
    VolumeCycle,
    Stop,
    WebSocketToggle,
    MicToggle,
    PushToTalk,
};

class SystemUIController {
public:
    SystemUIController();

    void begin(AIAvatar& avatar, const Config& config, StatusOverlay& statusOverlay);
    void update();
    void draw(LGFX_Sprite* canvas) const;
    bool menuOpen() const { return menuOpen_; }
    bool uiVisible() const { return uiVisible_; }
    void setVirtualButtonsEnabled(bool enabled) { virtualButtonsEnabled_ = enabled; }
    bool virtualButtonsEnabled() const { return virtualButtonsEnabled_; }
    void setTouchPushToTalkEnabled(bool enabled) { touchPushToTalkEnabled_ = enabled; }
    bool touchPushToTalkEnabled() const { return touchPushToTalkEnabled_; }
    void setVirtualButtonArea(ButtonId id, UiRect area);
    void setButtonAction(ButtonId id, ButtonAction action);
    void runButtonAction(ButtonId id);
    void setSystemBarHeight(int16_t height) { systemBarHeight_ = height; }
    void setMenuHorizontalMargin(int16_t margin) { menuHorizontalMargin_ = margin; }
    void setMenuItemHeight(int16_t height) { menuItemHeight_ = height; }
    void setMenuTextSize(uint8_t size) { menuTextSize_ = size; }
    void setMenuPadding(int16_t x, int16_t y) {
        menuPaddingX_ = x;
        menuPaddingY_ = y;
    }
    void onUnhandledTap(TapCallback cb) { unhandledTapCb_ = cb; }

private:
    static constexpr uint8_t kButtonCount = static_cast<uint8_t>(ButtonId::Count);

    AIAvatar* avatar_;
    const Config* config_;
    StatusOverlay* statusOverlay_;
    bool virtualButtonsEnabled_;
    bool touchPushToTalkEnabled_;
    UiRect virtualButtonAreas_[kButtonCount];
    ButtonAction buttonActions_[kButtonCount];
    bool uiVisible_;
    bool menuOpen_;
    bool menuClosePending_;
    uint8_t selected_;
    uint32_t menuAutoCloseMs_;
    bool touchActive_;
    bool touchHeld_;
    uint32_t touchStartMs_;
    int16_t touchStartX_;
    int16_t touchStartY_;
    int16_t touchLastX_;
    int16_t touchLastY_;
    int16_t systemBarHeight_;
    int16_t menuHorizontalMargin_;
    int16_t menuItemHeight_;
    uint8_t menuTextSize_;
    int16_t menuPaddingX_;
    int16_t menuPaddingY_;
    TapCallback unhandledTapCb_;

    static constexpr uint32_t kMenuAutoCloseMs = 10000;
    static constexpr uint32_t kMenuSelectCloseDelayMs = 600;
    static constexpr uint8_t kVolumeLevelCount = 5;
    static constexpr int16_t kMoveThreshold = 30;
    static constexpr int16_t kSwipeThreshold = 50;
    static constexpr int16_t kSwipeMaxHorizontal = 90;
    static constexpr int16_t kEdgeRevealHeight = 24;

    void recordTouch(const m5::touch_detail_t& detail);
    bool touchMovedBeyondTapThreshold() const;
    bool consumeSwipe(const m5::touch_detail_t& detail);
    void setUiVisible(bool visible);
    void handleTap(int16_t x, int16_t y);
    bool handleVirtualButtonTap(int16_t x, int16_t y);
    void updateHold(const m5::touch_detail_t& detail);
    bool hasPushToTalkButton() const;
    bool isPushToTalkTouch(int16_t x, int16_t y) const;
    void openMenu();
    void closeMenu();
    void handleMenuTap(int16_t x, int16_t y);
    void runMenuAction(uint8_t index);
    uint8_t menuItemCount() const;
    UiRect menuBounds() const;
    int8_t menuIndexAt(int16_t x, int16_t y) const;
    bool consumeTap(const m5::touch_detail_t& detail, int16_t& x, int16_t& y);
    bool isSystemBarTouch(int16_t y) const { return y <= systemBarHeight_; }
};

}  // namespace aiavatar
