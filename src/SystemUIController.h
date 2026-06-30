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

enum class UiButtonEvent : uint8_t {
    SingleClick = 0,
    DoubleClick,
    Hold,
    HoldRelease,
};

class SystemUIController {
public:
    SystemUIController();

    void begin(AIAvatar& avatar, const Config& config, StatusOverlay& statusOverlay);
    void update();
    void draw(LGFX_Sprite* canvas) const;
    bool menuOpen() const { return menuOpen_; }
    bool settingsOpen() const { return settingsOpen_; }
    bool toolMenuOpen() const { return toolMenuOpen_; }
    bool uiVisible() const { return uiVisible_; }
    // 更新処理中などにタッチ入力(戻るボタン含む)を全て無効化する。描画は継続する。
    void setInputLocked(bool locked);
    bool inputLocked() const { return inputLocked_; }
    void setVirtualButtonsEnabled(bool enabled) { virtualButtonsEnabled_ = enabled; }
    bool virtualButtonsEnabled() const { return virtualButtonsEnabled_; }
    void setTouchPushToTalkEnabled(bool enabled) { touchPushToTalkEnabled_ = enabled; }
    bool touchPushToTalkEnabled() const { return touchPushToTalkEnabled_; }
    void setVirtualButtonArea(ButtonId id, UiRect area);
    void setButtonAction(ButtonId id, ButtonAction action);
    void runButtonAction(ButtonId id);
    void handleButtonEvent(UiButtonEvent event);
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

    enum class SettingsItem : uint8_t {
        Brightness = 0,
        Mic,
        Speaker,
        WiFi,
        IdleMotion,
        Version,
        Count,
    };

    enum class SettingsView : uint8_t {
        Root = 0,
        Updates,
        Brightness,
        Speaker,
        WiFi,
        IdleMotion,
        Version,
        ToolUpdate,
    };

    enum class ToolView : uint8_t {
        Categories = 0,
        Tools,
    };

    enum class HoldTarget : uint8_t {
        None = 0,
        Brightness,
        Speaker,
        IdleMotionInterval,
    };

    enum class AtomSettingMode : uint8_t {
        HomeMute = 0,
        Volume,
        Brightness,
        WiFi,
        Count,
    };

    AIAvatar* avatar_;
    const Config* config_;
    StatusOverlay* statusOverlay_;
    bool virtualButtonsEnabled_;
    bool touchPushToTalkEnabled_;
    bool inputLocked_;
    UiRect virtualButtonAreas_[kButtonCount];
    ButtonAction buttonActions_[kButtonCount];
    bool uiVisible_;
    bool settingsOpen_;
    bool toolMenuOpen_;
    bool menuOpen_;
    bool menuClosePending_;
    uint8_t selected_;
    uint8_t settingsSelected_;
    SettingsView settingsView_;
    ToolView toolView_;
    uint8_t toolCategorySelected_;
    int16_t settingsScrollOffset_;
    int16_t wifiScrollOffset_;
    int16_t toolCategoryScrollOffset_;
    int16_t toolScrollOffset_;
    bool settingsHoldActive_;
    bool buttonNavigationActive_;
    bool uiAudioMuteActive_;
    AtomSettingMode atomSettingMode_;
    uint32_t atomSettingLastInputMs_;
    uint32_t atomFeedbackUntilMs_;
    char atomFeedbackText_[32];
    HoldTarget settingsHoldTarget_;
    int8_t settingsHoldDelta_;
    uint32_t settingsHoldNextMs_;
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
    static constexpr int16_t kSwipeMaxVertical = 80;
    static constexpr int16_t kEdgeRevealHeight = 24;
    static constexpr int16_t kSettingsHeaderHeight = 56;
    static constexpr int16_t kSettingsBackButtonWidth = 64;
    static constexpr int16_t kSettingsRowHeight = 46;
    static constexpr int16_t kSettingsRowPaddingX = 14;
    static constexpr int16_t kSettingsIconSize = 24;
    static constexpr int16_t kSettingsStepperButtonSize = 58;
    static constexpr uint32_t kSettingsHoldStartMs = 450;
    static constexpr uint32_t kSettingsHoldRepeatMs = 45;
    static constexpr uint32_t kAtomSettingIdleMs = 5000;
    static constexpr uint32_t kAtomFeedbackMs = 1000;
    static constexpr int8_t kAtomPercentStep = 10;

    void recordTouch(const m5::touch_detail_t& detail);
    void updateBuiltInButton();
    void updateAtomSettingMode();
    void beginUiAudioMute(bool speakerMuted = true);
    void restoreUiAudioMuteIfIdle();
    bool uiAudioMuteRequired() const;
    void toggleAudioMutePair();
    void handleButtonBack();
    void handleAtomButtonEvent(UiButtonEvent event);
    void advanceAtomSettingMode();
    void setAtomSettingMode(AtomSettingMode mode);
    void applyAtomSettingDelta(int8_t delta);
    void showAtomFeedback(const char* format, ...);
    int8_t currentWifiIndex() const;
    void switchAtomWifi(int8_t delta);
    void activateSelectedSetting();
    void advanceSettingsSelection();
    void ensureSettingsSelectionVisible();
    bool touchMovedBeyondTapThreshold() const;
    bool consumeSwipe(const m5::touch_detail_t& detail);
    void setUiVisible(bool visible);
    void openSettings();
    void closeSettings();
    void openToolMenu();
    void closeToolMenu();
    void handleToolBack();
    void handleTap(int16_t x, int16_t y);
    bool handleVirtualButtonTap(int16_t x, int16_t y);
    void updateHold(const m5::touch_detail_t& detail);
    bool hasPushToTalkButton() const;
    bool isPushToTalkTouch(int16_t x, int16_t y) const;
    void openMenu();
    void closeMenu();
    void handleMenuTap(int16_t x, int16_t y);
    void handleSettingsTap(int16_t x, int16_t y);
    void handleToolTap(int16_t x, int16_t y);
    bool updateSettingsHold(const m5::touch_detail_t& detail);
    void runMenuAction(uint8_t index);
    uint8_t menuItemCount() const;
    UiRect menuBounds() const;
    int8_t menuIndexAt(int16_t x, int16_t y) const;
    uint8_t settingsItemCount() const;
    uint8_t visibleSettingsRows() const;
    UiRect settingsBackBounds() const;
    UiRect settingsItemBounds(uint8_t index) const;
    int8_t settingsIndexAt(int16_t x, int16_t y) const;
    void runSettingsAction(uint8_t index);
    void handleSettingsBack();
    void handleSettingsRootTap(int16_t x, int16_t y);
    void handleBrightnessTap(int16_t x, int16_t y);
    void handleSpeakerTap(int16_t x, int16_t y);
    void handleWifiTap(int16_t x, int16_t y);
    void handleIdleMotionTap(int16_t x, int16_t y);
    void handleVersionTap(int16_t x, int16_t y);
    void handleUpdatesTap(int16_t x, int16_t y);
    void handleToolUpdateTap(int16_t x, int16_t y);
    void adjustBrightness(int8_t delta);
    void adjustSpeakerVolume(int8_t delta);
    void adjustIdleMotionInterval(int8_t delta);
    void adjustHoldTarget(HoldTarget target, int8_t delta);
    HoldTarget holdTargetAt(int16_t x, int16_t y, int8_t& delta) const;
    UiRect decrementButtonBounds() const;
    UiRect incrementButtonBounds() const;
    UiRect idleMotionToggleBounds() const;
    UiRect idleMotionTypeBounds() const;
    uint8_t visibleWifiRows() const;
    uint8_t wifiItemCount() const;
    UiRect wifiItemBounds(uint8_t visibleIndex) const;
    int8_t wifiIndexAt(int16_t x, int16_t y) const;
    uint8_t visibleToolRows() const;
    uint8_t toolCategoryCount() const;
    uint8_t toolItemCount() const;
    UiRect toolItemBounds(uint8_t visibleIndex) const;
    int8_t toolIndexAt(int16_t x, int16_t y, bool categories) const;
    void scrollSettings(int8_t delta);
    void scrollWifi(int8_t delta);
    void scrollTools(int8_t delta);
    const char* settingsTitle() const;
    const char* toolTitle() const;
    void drawNetworkMenu(LGFX_Sprite* canvas) const;
    void drawSettings(LGFX_Sprite* canvas) const;
    void drawToolMenu(LGFX_Sprite* canvas) const;
    void drawAtomSettingOverlay(LGFX_Sprite* canvas) const;
    void drawAtomHomeFeedback(LGFX_Sprite* canvas) const;
    bool atomFeedbackVisible() const;
    void drawSettingsHeader(LGFX_Sprite* canvas, const char* title) const;
    void drawSettingsRoot(LGFX_Sprite* canvas) const;
    void drawSettingsItem(LGFX_Sprite* canvas, uint8_t index) const;
    void drawBrightnessSettings(LGFX_Sprite* canvas) const;
    void drawSpeakerSettings(LGFX_Sprite* canvas) const;
    void drawWifiSettings(LGFX_Sprite* canvas) const;
    void drawIdleMotionSettings(LGFX_Sprite* canvas) const;
    void drawVersionSettings(LGFX_Sprite* canvas) const;
    void drawUpdatesSettings(LGFX_Sprite* canvas) const;
    void drawToolUpdateSettings(LGFX_Sprite* canvas) const;
    void drawUpdateMenuItem(LGFX_Sprite* canvas, uint8_t index, const char* label,
                            const char* value) const;
    void drawToolCategories(LGFX_Sprite* canvas) const;
    void drawToolList(LGFX_Sprite* canvas) const;
    void drawToolScrollIndicator(LGFX_Sprite* canvas, uint8_t total, uint8_t visible,
                                 int16_t offset) const;
    void drawStepper(LGFX_Sprite* canvas, int value, int minValue, int maxValue, const char* unit) const;
    void drawAdjustButton(LGFX_Sprite* canvas, UiRect bounds, char symbol) const;
    void drawSettingsIcon(LGFX_Sprite* canvas, SettingsItem item, UiRect bounds) const;
    UiRect otaCheckButtonBounds() const;
    UiRect otaUpdateButtonBounds() const;
    void drawOtaActionButton(LGFX_Sprite* canvas, UiRect bounds, const char* label, bool enabled) const;
    bool consumeTap(const m5::touch_detail_t& detail, int16_t& x, int16_t& y);
    bool isSystemBarTouch(int16_t y) const { return y <= systemBarHeight_; }
};

}  // namespace aiavatar
