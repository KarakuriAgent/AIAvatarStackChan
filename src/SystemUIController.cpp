#include "SystemUIController.h"

#include "AIAvatar.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

namespace aiavatar {

SystemUIController::SystemUIController()
    : avatar_(nullptr),
      config_(nullptr),
      statusOverlay_(nullptr),
      virtualButtonsEnabled_(true),
      touchPushToTalkEnabled_(true),
      virtualButtonAreas_{{0, 190, 72, 50}, {124, 190, 72, 50}, {248, 190, 72, 50}},
      buttonActions_{ButtonAction::VolumeCycle, ButtonAction::None, ButtonAction::None},
      uiVisible_(true),
      menuOpen_(false),
      menuClosePending_(false),
      selected_(0),
      menuAutoCloseMs_(0),
      touchActive_(false),
      touchHeld_(false),
      touchStartMs_(0),
      touchStartX_(0),
      touchStartY_(0),
      touchLastX_(0),
      touchLastY_(0),
      systemBarHeight_(36),
      menuHorizontalMargin_(40),
      menuItemHeight_(24),
      menuTextSize_(1),
      menuPaddingX_(12),
      menuPaddingY_(8),
      unhandledTapCb_(nullptr) {}

void SystemUIController::begin(AIAvatar& avatar, const Config& config,
                               StatusOverlay& statusOverlay) {
    avatar_ = &avatar;
    config_ = &config;
    statusOverlay_ = &statusOverlay;
}

void SystemUIController::update() {
    if (!avatar_ || !statusOverlay_) return;

    if (menuOpen_ && millis() >= menuAutoCloseMs_) {
        closeMenu();
    }

    if (!M5.Touch.isEnabled()) return;
    auto detail = M5.Touch.getDetail();
    recordTouch(detail);
    if (avatar_->wasSleepWakeTriggered()) return;
    updateHold(detail);
    if (consumeSwipe(detail)) return;

    int16_t tapX = 0;
    int16_t tapY = 0;
    if (!consumeTap(detail, tapX, tapY)) return;

    handleTap(tapX, tapY);
}

void SystemUIController::draw(LGFX_Sprite* canvas) const {
    if (!canvas || !menuOpen_ || !config_) return;

    uint8_t itemCount = menuItemCount();
    if (itemCount == 0) return;

    UiRect bounds = menuBounds();
    const int itemH = menuItemHeight_;
    const int paddingX = menuPaddingX_;
    const int paddingY = menuPaddingY_;
    const int textH = 8 * menuTextSize_;

    canvas->fillRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, 0x1082);
    canvas->drawRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, 0x4208);
    canvas->setTextSize(menuTextSize_);

    for (uint8_t i = 0; i < itemCount; ++i) {
        int itemY = bounds.y + paddingY + i * itemH;
        int textY = itemY + (itemH - textH) / 2;

        if (i == selected_) {
            canvas->fillRoundRect(bounds.x + 4, itemY + 2, bounds.w - 8, itemH - 4, 4, 0x001F);
            canvas->setTextColor(TFT_WHITE);
        } else {
            canvas->setTextColor(0xC618);
        }

        char label[72];
        char marker = '\0';
        if (i == 0) {
            snprintf(label, sizeof(label), "WS: %s", avatar_->isConnected() ? "ON" : "OFF");
        } else {
            uint8_t networkIndex = i - 1;
            const auto& network = config_->wifiNetworks[networkIndex];
            const char* displayName = network.name[0] ? network.name : network.ssid;
            snprintf(label, sizeof(label), "WiFi: %s", displayName);
            if (WiFi.status() == WL_CONNECTED && strcmp(WiFi.SSID().c_str(), network.ssid) == 0) {
                marker = '*';
            }
        }

        canvas->setCursor(bounds.x + paddingX, textY);
        canvas->print(label);
        if (marker) {
            canvas->setCursor(bounds.x + bounds.w - paddingX - 8, textY);
            canvas->print(marker);
        }
    }
}

void SystemUIController::setVirtualButtonArea(ButtonId id, UiRect area) {
    uint8_t index = static_cast<uint8_t>(id);
    if (index >= kButtonCount) return;
    virtualButtonAreas_[index] = area;
}

void SystemUIController::setButtonAction(ButtonId id, ButtonAction action) {
    uint8_t index = static_cast<uint8_t>(id);
    if (index >= kButtonCount) return;
    buttonActions_[index] = action;
}

void SystemUIController::runButtonAction(ButtonId id) {
    if (!avatar_) return;

    uint8_t index = static_cast<uint8_t>(id);
    if (index >= kButtonCount) return;

    switch (buttonActions_[index]) {
        case ButtonAction::VolumeCycle:
            avatar_->cycleVolume();
            break;
        case ButtonAction::Stop:
            avatar_->sendStop();
            break;
        case ButtonAction::WebSocketToggle:
            if (avatar_->isConnected()) {
                avatar_->disconnectWebSocket();
            } else {
                avatar_->connectWebSocket();
            }
            break;
        case ButtonAction::MicToggle:
            avatar_->toggleMicMuted();
            break;
        case ButtonAction::PushToTalk:
            break;
        case ButtonAction::None:
        default:
            break;
    }
}

void SystemUIController::handleTap(int16_t x, int16_t y) {
    if (!uiVisible_) return;

    if (menuOpen_) {
        handleMenuTap(x, y);
        return;
    }

    if (statusOverlay_->networkBounds().contains(x, y)) {
        openMenu();
        return;
    }
    if (statusOverlay_->micBounds().contains(x, y)) {
        avatar_->toggleMicMuted();
        return;
    }
    if (handleVirtualButtonTap(x, y)) {
        return;
    }
    if (unhandledTapCb_ && unhandledTapCb_(x, y)) {
        return;
    }
}

bool SystemUIController::handleVirtualButtonTap(int16_t x, int16_t y) {
    if (!uiVisible_ || !virtualButtonsEnabled_) return false;

    for (uint8_t i = 0; i < kButtonCount; ++i) {
        if (buttonActions_[i] == ButtonAction::None) continue;
        if (!virtualButtonAreas_[i].contains(x, y)) continue;
        runButtonAction(static_cast<ButtonId>(i));
        return true;
    }
    return false;
}

void SystemUIController::updateHold(const m5::touch_detail_t& detail) {
    if (menuOpen_) return;

    if (touchActive_ && !touchHeld_ && detail.isPressed()) {
        if (millis() - touchStartMs_ >= config_->pttHoldThresholdMs &&
            isPushToTalkTouch(touchStartX_, touchStartY_) &&
            !touchMovedBeyondTapThreshold()) {
            touchHeld_ = avatar_->startPushToTalk();
        }
    }

    if (touchActive_ && touchHeld_ && detail.wasReleased()) {
        avatar_->endPushToTalk();
        touchHeld_ = false;
        touchActive_ = false;
    }
}

bool SystemUIController::hasPushToTalkButton() const {
    if (!virtualButtonsEnabled_) return false;
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        if (buttonActions_[i] == ButtonAction::PushToTalk) return true;
    }
    return false;
}

bool SystemUIController::isPushToTalkTouch(int16_t x, int16_t y) const {
    if (!touchPushToTalkEnabled_) return false;
    if (hasPushToTalkButton()) {
        for (uint8_t i = 0; i < kButtonCount; ++i) {
            if (buttonActions_[i] != ButtonAction::PushToTalk) continue;
            if (virtualButtonAreas_[i].contains(x, y)) return true;
        }
        return false;
    }
    return !isSystemBarTouch(y);
}

void SystemUIController::openMenu() {
    if (!uiVisible_) return;

    menuOpen_ = true;
    menuClosePending_ = false;
    selected_ = 0;
    menuAutoCloseMs_ = millis() + kMenuAutoCloseMs;
    avatar_->display().setDirty();
}

void SystemUIController::closeMenu() {
    menuOpen_ = false;
    menuClosePending_ = false;
    avatar_->display().setDirty();
}

void SystemUIController::recordTouch(const m5::touch_detail_t& detail) {
    if (detail.wasPressed()) {
        avatar_->resetSleepTimer("touch");
        touchActive_ = true;
        touchHeld_ = false;
        touchStartMs_ = millis();
        touchStartX_ = detail.x;
        touchStartY_ = detail.y;
        touchLastX_ = detail.x;
        touchLastY_ = detail.y;
        return;
    }

    if (touchActive_ && detail.isPressed()) {
        touchLastX_ = detail.x;
        touchLastY_ = detail.y;
    }
}

bool SystemUIController::touchMovedBeyondTapThreshold() const {
    int16_t dx = touchLastX_ - touchStartX_;
    int16_t dy = touchLastY_ - touchStartY_;
    int32_t dist2 = static_cast<int32_t>(dx) * dx + static_cast<int32_t>(dy) * dy;
    return dist2 >= static_cast<int32_t>(kMoveThreshold) * kMoveThreshold;
}

bool SystemUIController::consumeSwipe(const m5::touch_detail_t& detail) {
    if (!detail.wasReleased() || !touchActive_ || touchHeld_) return false;

    int16_t dx = touchLastX_ - touchStartX_;
    int16_t dy = touchLastY_ - touchStartY_;
    bool vertical = abs(dx) <= kSwipeMaxHorizontal;
    bool hideGesture = uiVisible_ && vertical && dy <= -kSwipeThreshold;
    bool revealGesture = !uiVisible_ && touchStartY_ <= kEdgeRevealHeight &&
                         vertical && dy >= kSwipeThreshold;

    if (!hideGesture && !revealGesture) return false;

    touchActive_ = false;
    setUiVisible(revealGesture);
    return true;
}

void SystemUIController::setUiVisible(bool visible) {
    if (uiVisible_ == visible) return;

    uiVisible_ = visible;
    if (!uiVisible_ && menuOpen_) {
        menuOpen_ = false;
        menuClosePending_ = false;
    }
    avatar_->display().setDirty();
}

void SystemUIController::handleMenuTap(int16_t x, int16_t y) {
    if (menuClosePending_) return;

    int8_t index = menuIndexAt(x, y);
    if (index < 0) {
        closeMenu();
        return;
    }

    selected_ = static_cast<uint8_t>(index);
    menuClosePending_ = true;
    menuAutoCloseMs_ = millis() + kMenuSelectCloseDelayMs;
    avatar_->display().setDirty();
    runMenuAction(selected_);
}

void SystemUIController::runMenuAction(uint8_t index) {
    if (!avatar_) return;

    if (index == 0) {
        if (avatar_->isConnected()) {
            avatar_->disconnectWebSocket();
        } else {
            avatar_->connectWebSocket();
        }
        return;
    }

    avatar_->switchWiFi(index - 1);
}

uint8_t SystemUIController::menuItemCount() const {
    if (!config_) return 0;
    return 1 + config_->wifiNetworkCount;
}

UiRect SystemUIController::menuBounds() const {
    uint8_t itemCount = menuItemCount();
    const int itemH = menuItemHeight_;
    const int paddingY = menuPaddingY_;
    int displayW = M5.Display.width();
    int displayH = M5.Display.height();
    const int marginX = menuHorizontalMargin_;
    int menuW = displayW - marginX * 2;
    int menuH = itemCount * itemH + paddingY * 2;
    int menuX = marginX;
    int menuY = (displayH - menuH) / 2;
    return {static_cast<int16_t>(menuX), static_cast<int16_t>(menuY),
            static_cast<int16_t>(menuW), static_cast<int16_t>(menuH)};
}

int8_t SystemUIController::menuIndexAt(int16_t x, int16_t y) const {
    UiRect bounds = menuBounds();
    if (!bounds.contains(x, y)) return -1;

    const int itemH = menuItemHeight_;
    const int paddingY = menuPaddingY_;
    int localY = y - bounds.y - paddingY;
    if (localY < 0) return -1;
    uint8_t index = localY / itemH;
    if (index >= menuItemCount()) return -1;
    return static_cast<int8_t>(index);
}

bool SystemUIController::consumeTap(const m5::touch_detail_t& detail, int16_t& x, int16_t& y) {
    if (!detail.wasReleased() || !touchActive_ || touchHeld_) return false;

    int16_t dx = touchLastX_ - touchStartX_;
    int16_t dy = touchLastY_ - touchStartY_;
    int32_t dist2 = static_cast<int32_t>(dx) * dx + static_cast<int32_t>(dy) * dy;
    touchActive_ = false;
    if (dist2 >= static_cast<int32_t>(kMoveThreshold) * kMoveThreshold) {
        return false;
    }

    x = touchStartX_;
    y = touchStartY_;
    return true;
}

}  // namespace aiavatar
