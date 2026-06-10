#include "SystemUIController.h"

#include "AIAvatar.h"
#include "IdleMotionEstimator.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

namespace aiavatar {

namespace {

void copyTruncated(char* dest, size_t destSize, const char* src, size_t maxChars) {
    if (!dest || destSize == 0) return;
    strlcpy(dest, src ? src : "", destSize);
    size_t len = strlen(dest);
    if (len <= maxChars || maxChars + 1 > destSize) return;
    if (maxChars >= 3) {
        dest[maxChars - 3] = '.';
        dest[maxChars - 2] = '.';
        dest[maxChars - 1] = '.';
        dest[maxChars] = '\0';
    } else {
        dest[maxChars] = '\0';
    }
}



uint8_t byteToPercent(uint8_t value) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * 100 + 127) / 255);
}

uint8_t percentToByte(uint8_t percent) {
    if (percent > 100) percent = 100;
    return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255 + 50) / 100);
}

int clampPercent(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

const char* idleMotionTypeLabel(IdleMotionType type) {
    switch (type) {
        case IdleMotionType::Random:
            return "ランダム";
        case IdleMotionType::StereoBalance:
        default:
            return "左右差";
    }
}

}  // namespace

SystemUIController::SystemUIController()
    : avatar_(nullptr),
      config_(nullptr),
      statusOverlay_(nullptr),
      virtualButtonsEnabled_(true),
      touchPushToTalkEnabled_(true),
      virtualButtonAreas_{{0, 190, 72, 50}, {124, 190, 72, 50}, {248, 190, 72, 50}},
      buttonActions_{ButtonAction::VolumeCycle, ButtonAction::None, ButtonAction::None},
      uiVisible_(true),
      settingsOpen_(false),
      toolMenuOpen_(false),
      menuOpen_(false),
      menuClosePending_(false),
      selected_(0),
      settingsSelected_(0),
      settingsView_(SettingsView::Root),
      toolView_(ToolView::Categories),
      toolCategorySelected_(0),
      settingsScrollOffset_(0),
      wifiScrollOffset_(0),
      toolCategoryScrollOffset_(0),
      toolScrollOffset_(0),
      settingsHoldActive_(false),
      settingsHoldTarget_(HoldTarget::None),
      settingsHoldDelta_(0),
      settingsHoldNextMs_(0),
      menuAutoCloseMs_(0),
      touchActive_(false),
      touchHeld_(false),
      touchStartMs_(0),
      touchStartX_(0),
      touchStartY_(0),
      touchLastX_(0),
      touchLastY_(0),
      systemBarHeight_(44),
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
    if (updateSettingsHold(detail)) return;
    if (consumeSwipe(detail)) return;

    int16_t tapX = 0;
    int16_t tapY = 0;
    if (!consumeTap(detail, tapX, tapY)) return;

    handleTap(tapX, tapY);
}

void SystemUIController::draw(LGFX_Sprite* canvas) const {
    if (!canvas || !config_) return;

    if (settingsOpen_) {
        drawSettings(canvas);
    }
    if (toolMenuOpen_) {
        drawToolMenu(canvas);
    }
    if (menuOpen_) {
        drawNetworkMenu(canvas);
    }
}

void SystemUIController::drawNetworkMenu(LGFX_Sprite* canvas) const {
    if (!canvas || !config_) return;

    uint8_t itemCount = menuItemCount();
    if (itemCount == 0) return;

    UiRect bounds = menuBounds();
    const int itemH = menuItemHeight_;
    const int paddingX = menuPaddingX_;
    const int paddingY = menuPaddingY_;

    canvas->fillRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, 0x1082);
    canvas->drawRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, 0x4208);
    canvas->setFont(&fonts::lgfxJapanGothic_12);
    canvas->setTextSize(1);
    canvas->setTextDatum(top_left);

    for (uint8_t i = 0; i < itemCount; ++i) {
        int itemY = bounds.y + paddingY + i * itemH;
        int textY = itemY + (itemH - canvas->fontHeight()) / 2;

        if (i == selected_) {
            canvas->fillRoundRect(bounds.x + 4, itemY + 2, bounds.w - 8, itemH - 4, 4, 0x001F);
            canvas->setTextColor(TFT_WHITE);
        } else {
            canvas->setTextColor(0xC618);
        }

        char label[72];
        char marker = '\0';
        if (i == 0) {
            snprintf(label, sizeof(label), "WS: %s", avatar_->isConnected() ? "接続中" : "未接続");
        } else {
            uint8_t networkIndex = i - 1;
            const auto& network = config_->wifiNetworks[networkIndex];
            const char* displayName = network.name[0] ? network.name : network.ssid;
            snprintf(label, sizeof(label), "Wi-Fi: %s", displayName);
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

void SystemUIController::drawSettings(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    canvas->fillRect(0, 0, canvas->width(), canvas->height(), 0x0841);
    drawSettingsHeader(canvas, settingsTitle());

    switch (settingsView_) {
        case SettingsView::Root:
            drawSettingsRoot(canvas);
            break;
        case SettingsView::Updates:
            drawUpdatesSettings(canvas);
            break;
        case SettingsView::Brightness:
            drawBrightnessSettings(canvas);
            break;
        case SettingsView::Speaker:
            drawSpeakerSettings(canvas);
            break;
        case SettingsView::WiFi:
            drawWifiSettings(canvas);
            break;
        case SettingsView::IdleMotion:
            drawIdleMotionSettings(canvas);
            break;
        case SettingsView::Version:
            drawVersionSettings(canvas);
            break;
        case SettingsView::ToolUpdate:
            drawToolUpdateSettings(canvas);
            break;
    }
}

void SystemUIController::drawToolMenu(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    canvas->fillRect(0, 0, canvas->width(), canvas->height(), 0x0841);
    drawSettingsHeader(canvas, toolTitle());

    if (!avatar_->tools().hasTools()) {
        const char* message = "ツール設定がありません";
        canvas->setFont(&fonts::lgfxJapanGothic_20);
        canvas->setTextSize(1);
        canvas->setTextColor(0xC618);
        canvas->setTextDatum(top_center);
        canvas->drawString(message, canvas->width() / 2, kSettingsHeaderHeight + 56);
        return;
    }

    switch (toolView_) {
        case ToolView::Categories:
            drawToolCategories(canvas);
            break;
        case ToolView::Tools:
            drawToolList(canvas);
            break;
    }
}

void SystemUIController::drawSettingsHeader(LGFX_Sprite* canvas, const char* title) const {
    const int displayW = canvas->width();
    canvas->fillRect(0, 0, displayW, kSettingsHeaderHeight, TFT_BLACK);
    canvas->drawFastHLine(0, kSettingsHeaderHeight - 1, displayW, 0x4208);

    UiRect back = settingsBackBounds();
    const int cx = back.x + back.w / 2;
    const int cy = back.y + back.h / 2;
    canvas->drawLine(cx + 11, cy - 13, cx - 10, cy, TFT_WHITE);
    canvas->drawLine(cx - 10, cy, cx + 11, cy + 13, TFT_WHITE);
    canvas->drawFastHLine(cx - 10, cy, 27, TFT_WHITE);
    canvas->drawLine(cx + 11, cy - 12, cx - 9, cy, TFT_WHITE);
    canvas->drawLine(cx - 9, cy, cx + 11, cy + 12, TFT_WHITE);

    canvas->setFont(&fonts::lgfxJapanGothic_24);
    canvas->setTextSize(1);
    canvas->setTextDatum(top_left);
    canvas->setTextColor(TFT_WHITE);
    int y = (kSettingsHeaderHeight - canvas->fontHeight()) / 2;
    if (y < 0) y = 0;
    canvas->drawString(title, kSettingsBackButtonWidth, y);
}

void SystemUIController::drawSettingsRoot(LGFX_Sprite* canvas) const {
    uint8_t visibleRows = visibleSettingsRows();
    for (uint8_t visible = 0; visible < visibleRows; ++visible) {
        uint8_t index = settingsScrollOffset_ + visible;
        if (index >= settingsItemCount()) break;
        drawSettingsItem(canvas, index);
    }

    uint8_t total = settingsItemCount();
    if (total > visibleRows) {
        int indicatorX = canvas->width() - 5;
        int trackY = kSettingsHeaderHeight + 8;
        int trackH = canvas->height() - kSettingsHeaderHeight - 16;
        canvas->drawFastVLine(indicatorX, trackY, trackH, 0x4208);
        int thumbH = trackH * visibleRows / total;
        if (thumbH < 12) thumbH = 12;
        int maxOffset = total - visibleRows;
        int thumbY = trackY;
        if (maxOffset > 0) thumbY += (trackH - thumbH) * settingsScrollOffset_ / maxOffset;
        canvas->fillRoundRect(indicatorX - 2, thumbY, 4, thumbH, 2, 0xC618);
    }
}

void SystemUIController::drawSettingsItem(LGFX_Sprite* canvas, uint8_t index) const {
    if (!canvas || !avatar_) return;
    uint8_t visibleIndex = index >= settingsScrollOffset_ ? index - settingsScrollOffset_ : index;
    UiRect row = settingsItemBounds(visibleIndex);
    SettingsItem item = static_cast<SettingsItem>(index);

    canvas->fillRect(row.x, row.y, row.w, row.h, 0x0841);
    canvas->drawFastHLine(row.x + kSettingsRowPaddingX, row.y + row.h - 1,
                          row.w - kSettingsRowPaddingX * 2, 0x3186);

    UiRect iconBounds{static_cast<int16_t>(row.x + kSettingsRowPaddingX),
                      static_cast<int16_t>(row.y + (row.h - kSettingsIconSize) / 2),
                      kSettingsIconSize,
                      kSettingsIconSize};
    drawSettingsIcon(canvas, item, iconBounds);

    const char* label = "";
    char value[48] = "";
    bool navigates = true;
    switch (item) {
        case SettingsItem::Brightness: {
            label = "ライト";
            uint8_t pct = byteToPercent(avatar_->displayBrightness());
            snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(pct));
            break;
        }
        case SettingsItem::Mic:
            label = "マイク";
            snprintf(value, sizeof(value), "%s", avatar_->isMicMuted() ? "ミュート" : "オン");
            navigates = false;
            break;
        case SettingsItem::Speaker: {
            label = "スピーカー";
            if (avatar_->isSpeakerMuted()) {
                snprintf(value, sizeof(value), "ミュート");
            } else {
                uint8_t pct = byteToPercent(avatar_->currentVolume());
                snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(pct));
            }
            break;
        }
        case SettingsItem::WiFi:
            label = "Wi-Fi";
            if (WiFi.status() == WL_CONNECTED) {
                String ssid = WiFi.SSID();
                copyTruncated(value, sizeof(value), ssid.c_str(), 18);
            } else {
                snprintf(value, sizeof(value), "未接続");
            }
            break;
        case SettingsItem::IdleMotion:
            label = "待機モーション";
            snprintf(value, sizeof(value), "%s %s",
                     avatar_->idleMotionEnabled() ? "オン" : "オフ",
                     idleMotionTypeLabel(avatar_->idleMotionType()));
            break;
        case SettingsItem::Version:
            label = "アップデート";
            snprintf(value, sizeof(value), "FW / ツール");
            break;
        case SettingsItem::Count:
        default:
            break;
    }

    int labelX = row.x + kSettingsRowPaddingX + kSettingsIconSize + 12;
    canvas->setFont(&fonts::lgfxJapanGothic_20);
    canvas->setTextSize(1);
    int labelY = row.y + (row.h - canvas->fontHeight()) / 2;
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextDatum(top_left);
    canvas->drawString(label, labelX, labelY);

    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextColor(0xC618);
    int valueW = canvas->textWidth(value);
    int valueX = row.x + row.w - kSettingsRowPaddingX - valueW - (navigates ? 18 : 0);
    int valueY = row.y + (row.h - canvas->fontHeight()) / 2;
    canvas->drawString(value, valueX, valueY);

    if (navigates) {
        int cy = row.y + row.h / 2;
        int x = row.x + row.w - kSettingsRowPaddingX - 9;
        canvas->drawLine(x - 4, cy - 7, x + 4, cy, 0xC618);
        canvas->drawLine(x + 4, cy, x - 4, cy + 7, 0xC618);
    }
}

void SystemUIController::drawToolCategories(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    uint8_t total = toolCategoryCount();
    uint8_t visibleRows = visibleToolRows();
    for (uint8_t visible = 0; visible < visibleRows; ++visible) {
        uint8_t index = toolCategoryScrollOffset_ + visible;
        if (index >= total) break;

        UiRect row = toolItemBounds(visible);
        canvas->fillRect(row.x, row.y, row.w, row.h, 0x0841);
        canvas->drawFastHLine(row.x + kSettingsRowPaddingX, row.y + row.h - 1,
                              row.w - kSettingsRowPaddingX * 2, 0x3186);

        char name[48];
        copyTruncated(name, sizeof(name), avatar_->tools().categoryName(index), 18);
        canvas->setFont(&fonts::lgfxJapanGothic_20);
        canvas->setTextSize(1);
        canvas->setTextDatum(top_left);
        canvas->setTextColor(TFT_WHITE);
        int nameY = row.y + (row.h - canvas->fontHeight()) / 2;
        canvas->drawString(name, row.x + kSettingsRowPaddingX, nameY);

        char count[16];
        snprintf(count, sizeof(count), "%u", avatar_->tools().toolCount(index));
        canvas->setFont(&fonts::lgfxJapanGothic_16);
        canvas->setTextColor(0xC618);
        int countW = canvas->textWidth(count);
        int countY = row.y + (row.h - canvas->fontHeight()) / 2;
        canvas->drawString(count, row.x + row.w - kSettingsRowPaddingX - countW - 18, countY);

        int cy = row.y + row.h / 2;
        int x = row.x + row.w - kSettingsRowPaddingX - 9;
        canvas->drawLine(x - 4, cy - 7, x + 4, cy, 0xC618);
        canvas->drawLine(x + 4, cy, x - 4, cy + 7, 0xC618);
    }

    drawToolScrollIndicator(canvas, total, visibleRows, toolCategoryScrollOffset_);
}

void SystemUIController::drawToolList(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    uint8_t total = toolItemCount();
    if (total == 0) {
        const char* message = "ツールがありません";
        canvas->setFont(&fonts::lgfxJapanGothic_20);
        canvas->setTextSize(1);
        canvas->setTextColor(0xC618);
        canvas->setTextDatum(top_center);
        canvas->drawString(message, canvas->width() / 2, kSettingsHeaderHeight + 56);
        return;
    }

    uint8_t visibleRows = visibleToolRows();
    for (uint8_t visible = 0; visible < visibleRows; ++visible) {
        uint8_t index = toolScrollOffset_ + visible;
        if (index >= total) break;

        UiRect row = toolItemBounds(visible);
        canvas->fillRect(row.x, row.y, row.w, row.h, 0x0841);
        canvas->drawFastHLine(row.x + kSettingsRowPaddingX, row.y + row.h - 1,
                              row.w - kSettingsRowPaddingX * 2, 0x3186);

        char label[48];
        char name[48];
        copyTruncated(label, sizeof(label),
                      avatar_->tools().toolLabel(toolCategorySelected_, index), 18);
        copyTruncated(name, sizeof(name),
                      avatar_->tools().toolName(toolCategorySelected_, index), 24);

        canvas->setTextDatum(top_left);
        canvas->setTextSize(1);
        canvas->setFont(&fonts::lgfxJapanGothic_16);
        canvas->setTextColor(TFT_WHITE);
        canvas->drawString(label, row.x + kSettingsRowPaddingX, row.y + 6);

        if (strcmp(label, name) != 0 && name[0]) {
            canvas->setFont(&fonts::lgfxJapanGothic_12);
            canvas->setTextColor(0x8410);
            canvas->drawString(name, row.x + kSettingsRowPaddingX, row.y + 27);
        }

        int cy = row.y + row.h / 2;
        int x = row.x + row.w - kSettingsRowPaddingX - 15;
        canvas->fillTriangle(x - 3, cy - 8, x - 3, cy + 8, x + 9, cy, TFT_GREEN);
    }

    drawToolScrollIndicator(canvas, total, visibleRows, toolScrollOffset_);
}

void SystemUIController::drawToolScrollIndicator(LGFX_Sprite* canvas, uint8_t total,
                                                 uint8_t visible, int16_t offset) const {
    if (!canvas || total <= visible) return;

    int indicatorX = canvas->width() - 5;
    int trackY = kSettingsHeaderHeight + 8;
    int trackH = canvas->height() - kSettingsHeaderHeight - 16;
    canvas->drawFastVLine(indicatorX, trackY, trackH, 0x4208);
    int thumbH = trackH * visible / total;
    if (thumbH < 12) thumbH = 12;
    int maxOffset = total - visible;
    int thumbY = trackY;
    if (maxOffset > 0) thumbY += (trackH - thumbH) * offset / maxOffset;
    canvas->fillRoundRect(indicatorX - 2, thumbY, 4, thumbH, 2, 0xC618);
}

void SystemUIController::drawBrightnessSettings(LGFX_Sprite* canvas) const {
    drawStepper(canvas, byteToPercent(avatar_->displayBrightness()), 0, 100, "%");
}

void SystemUIController::drawSpeakerSettings(LGFX_Sprite* canvas) const {
    drawStepper(canvas, byteToPercent(avatar_->currentVolume()), 0, 100, "%");
}

void SystemUIController::drawIdleMotionSettings(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    UiRect row = idleMotionToggleBounds();
    canvas->fillRect(row.x, row.y, row.w, row.h, 0x0841);
    canvas->drawFastHLine(row.x + kSettingsRowPaddingX, row.y + row.h - 1,
                          row.w - kSettingsRowPaddingX * 2, 0x3186);

    const char* label = "有効";
    const char* value = avatar_->idleMotionEnabled() ? "オン" : "オフ";
    canvas->setFont(&fonts::lgfxJapanGothic_20);
    canvas->setTextSize(1);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextDatum(top_left);
    int labelY = row.y + (row.h - canvas->fontHeight()) / 2;
    canvas->drawString(label, row.x + kSettingsRowPaddingX, labelY);

    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextColor(0xC618);
    int valueW = canvas->textWidth(value);
    int valueY = row.y + (row.h - canvas->fontHeight()) / 2;
    canvas->drawString(value, row.x + row.w - kSettingsRowPaddingX - valueW, valueY);

    UiRect typeRow = idleMotionTypeBounds();
    canvas->fillRect(typeRow.x, typeRow.y, typeRow.w, typeRow.h, 0x0841);
    canvas->drawFastHLine(typeRow.x + kSettingsRowPaddingX, typeRow.y + typeRow.h - 1,
                          typeRow.w - kSettingsRowPaddingX * 2, 0x3186);

    const char* typeLabel = "タイプ";
    const char* typeValue = idleMotionTypeLabel(avatar_->idleMotionType());
    canvas->setFont(&fonts::lgfxJapanGothic_20);
    canvas->setTextColor(TFT_WHITE);
    int typeLabelY = typeRow.y + (typeRow.h - canvas->fontHeight()) / 2;
    canvas->drawString(typeLabel, typeRow.x + kSettingsRowPaddingX, typeLabelY);

    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextColor(0xC618);
    int typeValueW = canvas->textWidth(typeValue);
    int typeValueY = typeRow.y + (typeRow.h - canvas->fontHeight()) / 2;
    canvas->drawString(typeValue, typeRow.x + typeRow.w - kSettingsRowPaddingX - typeValueW,
                       typeValueY);

    char valueText[32];
    snprintf(valueText, sizeof(valueText), "%u秒",
             avatar_->idleMotionIntervalSeconds());
    canvas->setFont(&fonts::lgfxJapanGothic_36);
    canvas->setTextSize(1);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextDatum(top_center);
    int valueTextY = typeRow.y + typeRow.h + 4;
    canvas->drawString(valueText, canvas->width() / 2, valueTextY);

    char rangeText[32];
    snprintf(rangeText, sizeof(rangeText), "%u - %u秒",
             kIdleMotionIntervalMinSeconds, kIdleMotionIntervalMaxSeconds);
    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextColor(0xC618);
    canvas->drawString(rangeText, canvas->width() / 2, valueTextY + 48);

    drawAdjustButton(canvas, decrementButtonBounds(), '-');
    drawAdjustButton(canvas, incrementButtonBounds(), '+');
}

void SystemUIController::drawWifiSettings(LGFX_Sprite* canvas) const {
    if (!canvas || !config_) return;

    uint8_t total = wifiItemCount();
    if (total == 0) {
        const char* message = "Wi-Fi設定がありません";
        canvas->setFont(&fonts::lgfxJapanGothic_20);
        canvas->setTextSize(1);
        canvas->setTextColor(0xC618);
        canvas->setTextDatum(top_center);
        canvas->drawString(message, canvas->width() / 2, kSettingsHeaderHeight + 56);
        return;
    }

    uint8_t visibleRows = visibleWifiRows();
    for (uint8_t visible = 0; visible < visibleRows; ++visible) {
        uint8_t index = wifiScrollOffset_ + visible;
        if (index >= total) break;
        const auto& network = config_->wifiNetworks[index];
        UiRect row = wifiItemBounds(visible);
        bool active = WiFi.status() == WL_CONNECTED && strcmp(WiFi.SSID().c_str(), network.ssid) == 0;

        canvas->fillRect(row.x, row.y, row.w, row.h, active ? 0x1024 : 0x0841);
        canvas->drawFastHLine(row.x + kSettingsRowPaddingX, row.y + row.h - 1,
                              row.w - kSettingsRowPaddingX * 2, 0x3186);

        const char* displayName = network.name[0] ? network.name : network.ssid;
        char name[64];
        copyTruncated(name, sizeof(name), displayName, 20);
        canvas->setFont(&fonts::lgfxJapanGothic_16);
        canvas->setTextSize(1);
        canvas->setTextDatum(top_left);
        canvas->setTextColor(TFT_WHITE);
        int nameY = row.y + (row.h - canvas->fontHeight()) / 2;
        canvas->drawString(name, row.x + kSettingsRowPaddingX, nameY);

        if (active) {
            const char* status = "接続中";
            canvas->setFont(&fonts::lgfxJapanGothic_16);
            canvas->setTextColor(TFT_GREEN);
            int statusW = canvas->textWidth(status);
            int statusY = row.y + (row.h - canvas->fontHeight()) / 2;
            canvas->drawString(status, row.x + row.w - kSettingsRowPaddingX - statusW, statusY);
        }
    }

    if (total > visibleRows) {
        int indicatorX = canvas->width() - 5;
        int trackY = kSettingsHeaderHeight + 8;
        int trackH = canvas->height() - kSettingsHeaderHeight - 16;
        canvas->drawFastVLine(indicatorX, trackY, trackH, 0x4208);
        int thumbH = trackH * visibleRows / total;
        if (thumbH < 12) thumbH = 12;
        int maxOffset = total - visibleRows;
        int thumbY = trackY;
        if (maxOffset > 0) thumbY += (trackH - thumbH) * wifiScrollOffset_ / maxOffset;
        canvas->fillRoundRect(indicatorX - 2, thumbY, 4, thumbH, 2, 0xC618);
    }
}

void SystemUIController::drawVersionSettings(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    const int left = 18;
    int y = kSettingsHeaderHeight + 10;
    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextSize(1);
    canvas->setTextDatum(top_left);

    auto drawLabelValue = [&](const char* label, const char* value) {
        char text[96];
        snprintf(text, sizeof(text), "%s: %s", label, value && value[0] ? value : "-");
        canvas->setTextColor(0xC618);
        canvas->drawString(text, left, y);
        y += 20;
    };

    drawLabelValue("現在", avatar_->firmwareVersion());
    drawLabelValue("リリース", avatar_->firmwareReleaseDate());

    const OtaManifest& manifest = avatar_->otaManifest();
    if (manifest.version[0]) {
        char latest[72];
        if (manifest.releaseDate[0]) {
            snprintf(latest, sizeof(latest), "%s %s", manifest.version, manifest.releaseDate);
        } else {
            strlcpy(latest, manifest.version, sizeof(latest));
        }
        drawLabelValue("最新", latest);
    } else {
        drawLabelValue("最新", "未確認");
    }

    char status[96];
    copyTruncated(status, sizeof(status), avatar_->otaStatusMessage(), 28);
    drawLabelValue("状態", status);

    int progress = avatar_->otaProgressPercent();
    if (avatar_->otaUpdateStatus() == OtaUpdateStatus::Updating && progress >= 0) {
        int barX = left;
        int barY = y + 2;
        int barW = canvas->width() - left * 2;
        int barH = 10;
        canvas->drawRoundRect(barX, barY, barW, barH, 3, 0x8410);
        int fillW = (barW - 2) * progress / 100;
        if (fillW > 0) canvas->fillRoundRect(barX + 1, barY + 1, fillW, barH - 2, 2, TFT_GREEN);
    }

    bool busy = avatar_->otaBusy();
    drawOtaActionButton(canvas, otaCheckButtonBounds(), "アップデート確認", !busy);
    drawOtaActionButton(canvas, otaUpdateButtonBounds(), "アップデート実行",
                        !busy && avatar_->otaUpdateAvailable());
}

void SystemUIController::drawUpdatesSettings(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    char firmwareValue[48];
    const OtaManifest& firmwareManifest = avatar_->otaManifest();
    if (avatar_->otaUpdateAvailable() && firmwareManifest.version[0]) {
        snprintf(firmwareValue, sizeof(firmwareValue), "%s", firmwareManifest.version);
    } else {
        copyTruncated(firmwareValue, sizeof(firmwareValue), avatar_->otaStatusMessage(), 18);
    }

    char toolValue[48];
    const ToolManifest& toolManifest = avatar_->toolManifest();
    if (avatar_->toolUpdateAvailable() && toolManifest.version[0]) {
        snprintf(toolValue, sizeof(toolValue), "%s", toolManifest.version);
    } else {
        copyTruncated(toolValue, sizeof(toolValue), avatar_->toolUpdateStatusMessage(), 18);
    }

    drawUpdateMenuItem(canvas, 0, "ファームウェア", firmwareValue);
    drawUpdateMenuItem(canvas, 1, "ツール設定", toolValue);
}

void SystemUIController::drawToolUpdateSettings(LGFX_Sprite* canvas) const {
    if (!canvas || !avatar_) return;

    const int left = 18;
    int y = kSettingsHeaderHeight + 10;
    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextSize(1);
    canvas->setTextDatum(top_left);

    auto drawLabelValue = [&](const char* label, const char* value) {
        char text[96];
        snprintf(text, sizeof(text), "%s: %s", label, value && value[0] ? value : "-");
        canvas->setTextColor(0xC618);
        canvas->drawString(text, left, y);
        y += 20;
    };

    drawLabelValue("現在", avatar_->toolLocalVersion());

    const ToolManifest& manifest = avatar_->toolManifest();
    if (manifest.version[0]) {
        char latest[72];
        if (manifest.releaseDate[0]) {
            snprintf(latest, sizeof(latest), "%s %s", manifest.version, manifest.releaseDate);
        } else {
            strlcpy(latest, manifest.version, sizeof(latest));
        }
        drawLabelValue("最新", latest);
    } else {
        drawLabelValue("最新", "未確認");
    }

    char status[96];
    copyTruncated(status, sizeof(status), avatar_->toolUpdateStatusMessage(), 28);
    drawLabelValue("状態", status);

    int progress = avatar_->toolUpdateProgressPercent();
    if (avatar_->toolUpdateStatus() == OtaUpdateStatus::Updating && progress >= 0) {
        int barX = left;
        int barY = y + 2;
        int barW = canvas->width() - left * 2;
        int barH = 10;
        canvas->drawRoundRect(barX, barY, barW, barH, 3, 0x8410);
        int fillW = (barW - 2) * progress / 100;
        if (fillW > 0) canvas->fillRoundRect(barX + 1, barY + 1, fillW, barH - 2, 2, TFT_GREEN);
    }

    bool busy = avatar_->toolUpdateBusy();
    drawOtaActionButton(canvas, otaCheckButtonBounds(), "更新確認", !busy);
    drawOtaActionButton(canvas, otaUpdateButtonBounds(), "更新実行",
                        !busy && avatar_->toolUpdateAvailable());
}

void SystemUIController::drawUpdateMenuItem(LGFX_Sprite* canvas, uint8_t index,
                                            const char* label, const char* value) const {
    if (!canvas) return;

    UiRect row = settingsItemBounds(index);
    canvas->fillRect(row.x, row.y, row.w, row.h, 0x0841);
    canvas->drawFastHLine(row.x + kSettingsRowPaddingX, row.y + row.h - 1,
                          row.w - kSettingsRowPaddingX * 2, 0x3186);

    canvas->setFont(&fonts::lgfxJapanGothic_20);
    canvas->setTextSize(1);
    canvas->setTextDatum(top_left);
    canvas->setTextColor(TFT_WHITE);
    int labelY = row.y + (row.h - canvas->fontHeight()) / 2;
    canvas->drawString(label, row.x + kSettingsRowPaddingX, labelY);

    char clipped[48];
    copyTruncated(clipped, sizeof(clipped), value, 18);
    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextColor(0xC618);
    int valueW = canvas->textWidth(clipped);
    int valueY = row.y + (row.h - canvas->fontHeight()) / 2;
    canvas->drawString(clipped, row.x + row.w - kSettingsRowPaddingX - valueW - 18, valueY);

    int cy = row.y + row.h / 2;
    int x = row.x + row.w - kSettingsRowPaddingX - 9;
    canvas->drawLine(x - 4, cy - 7, x + 4, cy, 0xC618);
    canvas->drawLine(x + 4, cy, x - 4, cy + 7, 0xC618);
}

void SystemUIController::drawStepper(LGFX_Sprite* canvas, int value, int minValue, int maxValue,
                                     const char* unit) const {
    if (!canvas) return;

    char valueText[32];
    snprintf(valueText, sizeof(valueText), "%d%s", value, unit ? unit : "");

    canvas->setFont(&fonts::lgfxJapanGothic_36);
    canvas->setTextSize(1);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextDatum(top_center);
    int valueY = kSettingsHeaderHeight + 34;
    canvas->drawString(valueText, canvas->width() / 2, valueY);

    char rangeText[32];
    snprintf(rangeText, sizeof(rangeText), "%d - %d", minValue, maxValue);
    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextColor(0xC618);
    canvas->drawString(rangeText, canvas->width() / 2, valueY + 48);

    drawAdjustButton(canvas, decrementButtonBounds(), '-');
    drawAdjustButton(canvas, incrementButtonBounds(), '+');
}

void SystemUIController::drawAdjustButton(LGFX_Sprite* canvas, UiRect bounds, char symbol) const {
    const uint16_t fill = 0x2104;
    const uint16_t outline = 0x8410;
    canvas->fillRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, fill);
    canvas->drawRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, outline);
    canvas->setFont(&fonts::Font4);
    canvas->setTextSize(1);
    canvas->setTextDatum(middle_center);
    canvas->setTextColor(TFT_WHITE);
    char text[2] = {symbol, '\0'};
    canvas->drawString(text, bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
}

void SystemUIController::drawSettingsIcon(LGFX_Sprite* canvas, SettingsItem item,
                                          UiRect bounds) const {
    if (!canvas) return;
    const int x = bounds.x;
    const int y = bounds.y;
    const int s = bounds.w;
    const int cx = x + s / 2;
    const int cy = y + s / 2;
    const uint16_t color = TFT_WHITE;

    switch (item) {
        case SettingsItem::Brightness:
            canvas->drawCircle(cx, cy, s / 5, color);
            canvas->fillCircle(cx, cy, s / 8, TFT_YELLOW);
            canvas->drawFastHLine(x, cy, s / 4, color);
            canvas->drawFastHLine(cx + s / 4, cy, s / 4, color);
            canvas->drawFastVLine(cx, y, s / 4, color);
            canvas->drawFastVLine(cx, cy + s / 4, s / 4, color);
            canvas->drawLine(x + 4, y + 4, x + 8, y + 8, color);
            canvas->drawLine(x + s - 5, y + 4, x + s - 9, y + 8, color);
            canvas->drawLine(x + 4, y + s - 5, x + 8, y + s - 9, color);
            canvas->drawLine(x + s - 5, y + s - 5, x + s - 9, y + s - 9, color);
            break;
        case SettingsItem::Mic:
            canvas->fillRoundRect(cx - 3, y + 3, 6, 13, 3, color);
            canvas->drawRoundRect(cx - 7, y + 9, 14, 10, 4, color);
            canvas->drawFastVLine(cx, y + 18, 4, color);
            canvas->drawFastHLine(cx - 5, y + 22, 10, color);
            break;
        case SettingsItem::Speaker:
            canvas->fillRect(x + 2, y + 9, 5, 8, color);
            canvas->fillTriangle(x + 7, y + 9, x + 14, y + 4, x + 14, y + 20, color);
            canvas->drawLine(x + 17, y + 8, x + 20, cy, color);
            canvas->drawLine(x + 20, cy, x + 17, y + 16, color);
            canvas->drawLine(x + 20, y + 5, x + 23, cy, color);
            canvas->drawLine(x + 23, cy, x + 20, y + 19, color);
            break;
        case SettingsItem::WiFi:
            canvas->drawLine(cx - 10, y + 9, cx - 6, y + 6, color);
            canvas->drawLine(cx - 6, y + 6, cx, y + 5, color);
            canvas->drawLine(cx, y + 5, cx + 6, y + 6, color);
            canvas->drawLine(cx + 6, y + 6, cx + 10, y + 9, color);
            canvas->drawLine(cx - 6, y + 13, cx - 3, y + 11, color);
            canvas->drawLine(cx - 3, y + 11, cx, y + 10, color);
            canvas->drawLine(cx, y + 10, cx + 3, y + 11, color);
            canvas->drawLine(cx + 3, y + 11, cx + 6, y + 13, color);
            canvas->fillCircle(cx, y + 18, 2, color);
            break;
        case SettingsItem::IdleMotion:
            canvas->drawCircle(cx, cy, s / 3, color);
            canvas->drawLine(cx, cy, cx + 7, cy - 6, color);
            canvas->drawLine(cx, cy, cx - 6, cy + 5, color);
            canvas->drawTriangle(cx + 8, cy - 9, cx + 8, cy - 3, cx + 13, cy - 6, color);
            canvas->fillTriangle(cx - 8, cy + 9, cx - 8, cy + 3, cx - 13, cy + 6, color);
            break;
        case SettingsItem::Version:
            canvas->drawRoundRect(x + 4, y + 3, s - 8, s - 6, 3, color);
            canvas->setFont(&fonts::Font2);
            canvas->setTextSize(1);
            canvas->setTextDatum(middle_center);
            canvas->setTextColor(color);
            canvas->drawString("V", cx, cy + 1);
            break;
        case SettingsItem::Count:
        default:
            break;
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
    if (!settingsOpen_ && !toolMenuOpen_ && uiVisible_ &&
        statusOverlay_->speakerBounds().contains(x, y)) {
        avatar_->toggleSpeakerMuted();
        return;
    }

    if (toolMenuOpen_) {
        handleToolTap(x, y);
        return;
    }

    if (avatar_->cancelPlayback()) {
        return;
    }

    if (settingsOpen_) {
        handleSettingsTap(x, y);
        return;
    }

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
    if (menuOpen_ || settingsOpen_ || toolMenuOpen_) return;

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

bool SystemUIController::updateSettingsHold(const m5::touch_detail_t& detail) {
    if (!settingsOpen_ || !avatar_) return false;
    if (settingsView_ != SettingsView::Brightness && settingsView_ != SettingsView::Speaker &&
        settingsView_ != SettingsView::IdleMotion) {
        settingsHoldActive_ = false;
        settingsHoldTarget_ = HoldTarget::None;
        return false;
    }

    if (detail.wasReleased()) {
        if (!settingsHoldActive_) return false;
        settingsHoldActive_ = false;
        settingsHoldTarget_ = HoldTarget::None;
        settingsHoldDelta_ = 0;
        touchHeld_ = false;
        touchActive_ = false;
        return true;
    }

    if (!detail.isPressed()) return false;

    if (!settingsHoldActive_) {
        if (millis() - touchStartMs_ < kSettingsHoldStartMs) return false;
        if (touchMovedBeyondTapThreshold()) return false;
        int8_t delta = 0;
        HoldTarget target = holdTargetAt(touchStartX_, touchStartY_, delta);
        if (target == HoldTarget::None) return false;
        settingsHoldActive_ = true;
        settingsHoldTarget_ = target;
        settingsHoldDelta_ = delta;
        settingsHoldNextMs_ = 0;
        touchHeld_ = true;
    }

    uint32_t now = millis();
    if (settingsHoldNextMs_ == 0 || static_cast<int32_t>(now - settingsHoldNextMs_) >= 0) {
        adjustHoldTarget(settingsHoldTarget_, settingsHoldDelta_);
        settingsHoldNextMs_ = now + kSettingsHoldRepeatMs;
    }
    return true;
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

void SystemUIController::openSettings() {
    if (!uiVisible_) return;

    settingsOpen_ = true;
    toolMenuOpen_ = false;
    settingsView_ = SettingsView::Root;
    menuOpen_ = false;
    menuClosePending_ = false;
    settingsSelected_ = 0;
    settingsScrollOffset_ = 0;
    settingsHoldActive_ = false;
    settingsHoldTarget_ = HoldTarget::None;
    avatar_->motion().goHome();
    avatar_->display().setDirty();
}

void SystemUIController::closeSettings() {
    if (!settingsOpen_) return;

    settingsOpen_ = false;
    settingsView_ = SettingsView::Root;
    menuOpen_ = false;
    menuClosePending_ = false;
    settingsHoldActive_ = false;
    settingsHoldTarget_ = HoldTarget::None;
    avatar_->display().setDirty();
}

void SystemUIController::openToolMenu() {
    if (!uiVisible_) return;

    toolMenuOpen_ = true;
    settingsOpen_ = false;
    settingsView_ = SettingsView::Root;
    settingsHoldActive_ = false;
    settingsHoldTarget_ = HoldTarget::None;
    menuOpen_ = false;
    menuClosePending_ = false;
    toolView_ = ToolView::Categories;
    toolCategorySelected_ = 0;
    toolCategoryScrollOffset_ = 0;
    toolScrollOffset_ = 0;
    avatar_->motion().goHome();
    avatar_->display().setDirty();
}

void SystemUIController::closeToolMenu() {
    if (!toolMenuOpen_) return;

    toolMenuOpen_ = false;
    toolView_ = ToolView::Categories;
    toolCategorySelected_ = 0;
    toolCategoryScrollOffset_ = 0;
    toolScrollOffset_ = 0;
    menuOpen_ = false;
    menuClosePending_ = false;
    avatar_->display().setDirty();
}

void SystemUIController::handleToolBack() {
    if (toolView_ == ToolView::Tools) {
        toolView_ = ToolView::Categories;
        toolScrollOffset_ = 0;
        avatar_->display().setDirty();
        return;
    }
    closeToolMenu();
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
        settingsHoldActive_ = false;
        settingsHoldTarget_ = HoldTarget::None;
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
    bool horizontal = abs(dy) <= kSwipeMaxVertical && abs(dx) > abs(dy);

    if (toolMenuOpen_) {
        if (horizontal && dx <= -kSwipeThreshold) {
            touchActive_ = false;
            handleToolBack();
            return true;
        }
        bool vertical = abs(dx) <= kSwipeMaxHorizontal && abs(dy) >= kSwipeThreshold;
        if (vertical) {
            touchActive_ = false;
            scrollTools(dy < 0 ? 1 : -1);
            return true;
        }
        return false;
    }

    if (settingsOpen_) {
        if (horizontal && dx >= kSwipeThreshold) {
            touchActive_ = false;
            handleSettingsBack();
            return true;
        }
        bool vertical = abs(dx) <= kSwipeMaxHorizontal && abs(dy) >= kSwipeThreshold;
        if (vertical && settingsView_ == SettingsView::Root) {
            touchActive_ = false;
            scrollSettings(dy < 0 ? 1 : -1);
            return true;
        }
        if (vertical && settingsView_ == SettingsView::WiFi) {
            touchActive_ = false;
            scrollWifi(dy < 0 ? 1 : -1);
            return true;
        }
        return false;
    }

    if (!menuOpen_ && uiVisible_ && horizontal && dx <= -kSwipeThreshold) {
        touchActive_ = false;
        openSettings();
        return true;
    }

    if (!menuOpen_ && uiVisible_ && horizontal && dx >= kSwipeThreshold) {
        touchActive_ = false;
        openToolMenu();
        return true;
    }

    return false;
}

void SystemUIController::setUiVisible(bool visible) {
    if (uiVisible_ == visible) return;

    uiVisible_ = visible;
    if (!uiVisible_) {
        if (menuOpen_) {
            menuOpen_ = false;
            menuClosePending_ = false;
        }
        settingsOpen_ = false;
        settingsView_ = SettingsView::Root;
        toolMenuOpen_ = false;
        toolView_ = ToolView::Categories;
        toolCategorySelected_ = 0;
        toolCategoryScrollOffset_ = 0;
        toolScrollOffset_ = 0;
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

void SystemUIController::handleSettingsTap(int16_t x, int16_t y) {
    if (settingsBackBounds().contains(x, y)) {
        handleSettingsBack();
        return;
    }

    switch (settingsView_) {
        case SettingsView::Root:
            handleSettingsRootTap(x, y);
            break;
        case SettingsView::Updates:
            handleUpdatesTap(x, y);
            break;
        case SettingsView::Brightness:
            handleBrightnessTap(x, y);
            break;
        case SettingsView::Speaker:
            handleSpeakerTap(x, y);
            break;
        case SettingsView::WiFi:
            handleWifiTap(x, y);
            break;
        case SettingsView::IdleMotion:
            handleIdleMotionTap(x, y);
            break;
        case SettingsView::Version:
            handleVersionTap(x, y);
            break;
        case SettingsView::ToolUpdate:
            handleToolUpdateTap(x, y);
            break;
    }
}

void SystemUIController::handleToolTap(int16_t x, int16_t y) {
    if (!avatar_) return;

    if (settingsBackBounds().contains(x, y)) {
        handleToolBack();
        return;
    }

    if (toolView_ == ToolView::Categories) {
        int8_t index = toolIndexAt(x, y, true);
        if (index < 0) return;
        toolCategorySelected_ = static_cast<uint8_t>(index);
        toolView_ = ToolView::Tools;
        toolScrollOffset_ = 0;
        avatar_->display().setDirty();
        return;
    }

    int8_t index = toolIndexAt(x, y, false);
    if (index < 0) return;
    if (avatar_->tools().execute(toolCategorySelected_, static_cast<uint8_t>(index))) {
        closeToolMenu();
    }
}

void SystemUIController::handleSettingsRootTap(int16_t x, int16_t y) {
    int8_t index = settingsIndexAt(x, y);
    if (index < 0) return;

    settingsSelected_ = static_cast<uint8_t>(index);
    runSettingsAction(settingsSelected_);
    avatar_->display().setDirty();
}

void SystemUIController::handleBrightnessTap(int16_t x, int16_t y) {
    if (decrementButtonBounds().contains(x, y)) {
        adjustBrightness(-1);
    } else if (incrementButtonBounds().contains(x, y)) {
        adjustBrightness(1);
    }
}

void SystemUIController::handleSpeakerTap(int16_t x, int16_t y) {
    if (decrementButtonBounds().contains(x, y)) {
        adjustSpeakerVolume(-1);
    } else if (incrementButtonBounds().contains(x, y)) {
        adjustSpeakerVolume(1);
    }
}

void SystemUIController::handleWifiTap(int16_t x, int16_t y) {
    int8_t index = wifiIndexAt(x, y);
    if (index < 0) return;
    avatar_->switchWiFi(static_cast<uint8_t>(index));
    avatar_->display().setDirty();
}

void SystemUIController::handleIdleMotionTap(int16_t x, int16_t y) {
    if (idleMotionToggleBounds().contains(x, y)) {
        avatar_->toggleIdleMotionEnabled();
    } else if (idleMotionTypeBounds().contains(x, y)) {
        avatar_->cycleIdleMotionType();
    } else if (decrementButtonBounds().contains(x, y)) {
        adjustIdleMotionInterval(-1);
    } else if (incrementButtonBounds().contains(x, y)) {
        adjustIdleMotionInterval(1);
    }
}

void SystemUIController::handleVersionTap(int16_t x, int16_t y) {
    if (otaCheckButtonBounds().contains(x, y)) {
        avatar_->checkForFirmwareUpdate();
        return;
    }
    if (otaUpdateButtonBounds().contains(x, y) && avatar_->otaUpdateAvailable() && !avatar_->otaBusy()) {
        avatar_->startFirmwareUpdate();
    }
}

void SystemUIController::handleUpdatesTap(int16_t x, int16_t y) {
    (void)x;
    if (y < kSettingsHeaderHeight) return;
    uint8_t index = (y - kSettingsHeaderHeight) / kSettingsRowHeight;
    if (index == 0) {
        settingsView_ = SettingsView::Version;
        avatar_->display().setDirty();
    } else if (index == 1) {
        settingsView_ = SettingsView::ToolUpdate;
        avatar_->display().setDirty();
    }
}

void SystemUIController::handleToolUpdateTap(int16_t x, int16_t y) {
    if (otaCheckButtonBounds().contains(x, y)) {
        avatar_->checkForToolUpdate();
        return;
    }
    if (otaUpdateButtonBounds().contains(x, y) && avatar_->toolUpdateAvailable() &&
        !avatar_->toolUpdateBusy()) {
        avatar_->startToolUpdate();
    }
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

void SystemUIController::runSettingsAction(uint8_t index) {
    if (!avatar_) return;

    switch (static_cast<SettingsItem>(index)) {
        case SettingsItem::Brightness:
            settingsView_ = SettingsView::Brightness;
            break;
        case SettingsItem::Mic:
            avatar_->toggleMicMuted();
            break;
        case SettingsItem::Speaker:
            settingsView_ = SettingsView::Speaker;
            break;
        case SettingsItem::WiFi:
            settingsView_ = SettingsView::WiFi;
            wifiScrollOffset_ = 0;
            break;
        case SettingsItem::IdleMotion:
            settingsView_ = SettingsView::IdleMotion;
            break;
        case SettingsItem::Version:
            settingsView_ = SettingsView::Updates;
            break;
        case SettingsItem::Count:
        default:
            break;
    }
}

void SystemUIController::handleSettingsBack() {
    if (settingsView_ == SettingsView::Root) {
        closeSettings();
        return;
    }
    if (settingsView_ == SettingsView::Version || settingsView_ == SettingsView::ToolUpdate) {
        settingsView_ = SettingsView::Updates;
        avatar_->display().setDirty();
        return;
    }
    settingsView_ = SettingsView::Root;
    settingsHoldActive_ = false;
    settingsHoldTarget_ = HoldTarget::None;
    avatar_->display().setDirty();
}

void SystemUIController::adjustBrightness(int8_t delta) {
    int pct = clampPercent(byteToPercent(avatar_->displayBrightness()) + delta);
    avatar_->setDisplayBrightness(percentToByte(static_cast<uint8_t>(pct)));
}

void SystemUIController::adjustSpeakerVolume(int8_t delta) {
    int pct = clampPercent(byteToPercent(avatar_->currentVolume()) + delta);
    avatar_->setVolume(percentToByte(static_cast<uint8_t>(pct)));
}

void SystemUIController::adjustIdleMotionInterval(int8_t delta) {
    int seconds = static_cast<int>(avatar_->idleMotionIntervalSeconds()) + delta;
    avatar_->setIdleMotionIntervalSeconds(clampIdleMotionIntervalSeconds(seconds));
}

void SystemUIController::adjustHoldTarget(HoldTarget target, int8_t delta) {
    switch (target) {
        case HoldTarget::Brightness:
            adjustBrightness(delta);
            break;
        case HoldTarget::Speaker:
            adjustSpeakerVolume(delta);
            break;
        case HoldTarget::IdleMotionInterval:
            adjustIdleMotionInterval(delta);
            break;
        case HoldTarget::None:
        default:
            break;
    }
}

SystemUIController::HoldTarget SystemUIController::holdTargetAt(int16_t x, int16_t y,
                                                                int8_t& delta) const {
    if (decrementButtonBounds().contains(x, y)) {
        delta = -1;
    } else if (incrementButtonBounds().contains(x, y)) {
        delta = 1;
    } else {
        delta = 0;
        return HoldTarget::None;
    }

    if (settingsView_ == SettingsView::Brightness) return HoldTarget::Brightness;
    if (settingsView_ == SettingsView::Speaker) return HoldTarget::Speaker;
    if (settingsView_ == SettingsView::IdleMotion) return HoldTarget::IdleMotionInterval;
    delta = 0;
    return HoldTarget::None;
}

UiRect SystemUIController::decrementButtonBounds() const {
    int displayH = M5.Display.height();
    int y = displayH - kSettingsStepperButtonSize - 24;
    return {30, static_cast<int16_t>(y), kSettingsStepperButtonSize, kSettingsStepperButtonSize};
}

UiRect SystemUIController::incrementButtonBounds() const {
    int displayW = M5.Display.width();
    int displayH = M5.Display.height();
    int y = displayH - kSettingsStepperButtonSize - 24;
    return {static_cast<int16_t>(displayW - 30 - kSettingsStepperButtonSize),
            static_cast<int16_t>(y), kSettingsStepperButtonSize, kSettingsStepperButtonSize};
}

UiRect SystemUIController::idleMotionToggleBounds() const {
    return settingsItemBounds(0);
}

UiRect SystemUIController::idleMotionTypeBounds() const {
    return settingsItemBounds(1);
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

uint8_t SystemUIController::settingsItemCount() const {
    return static_cast<uint8_t>(SettingsItem::Count);
}

uint8_t SystemUIController::visibleSettingsRows() const {
    int rows = (M5.Display.height() - kSettingsHeaderHeight) / kSettingsRowHeight;
    if (rows < 1) return 1;
    return static_cast<uint8_t>(rows);
}

UiRect SystemUIController::settingsBackBounds() const {
    return {0, 0, kSettingsBackButtonWidth, kSettingsHeaderHeight};
}

UiRect SystemUIController::settingsItemBounds(uint8_t index) const {
    int displayW = M5.Display.width();
    int y = kSettingsHeaderHeight + index * kSettingsRowHeight;
    return {0, static_cast<int16_t>(y), static_cast<int16_t>(displayW), kSettingsRowHeight};
}

int8_t SystemUIController::settingsIndexAt(int16_t x, int16_t y) const {
    (void)x;
    if (y < kSettingsHeaderHeight) return -1;
    uint8_t visibleIndex = (y - kSettingsHeaderHeight) / kSettingsRowHeight;
    if (visibleIndex >= visibleSettingsRows()) return -1;
    uint8_t index = settingsScrollOffset_ + visibleIndex;
    if (index >= settingsItemCount()) return -1;
    return static_cast<int8_t>(index);
}

uint8_t SystemUIController::visibleWifiRows() const {
    int rows = (M5.Display.height() - kSettingsHeaderHeight) / kSettingsRowHeight;
    if (rows < 1) return 1;
    return static_cast<uint8_t>(rows);
}

uint8_t SystemUIController::wifiItemCount() const {
    return config_ ? config_->wifiNetworkCount : 0;
}

UiRect SystemUIController::wifiItemBounds(uint8_t visibleIndex) const {
    int displayW = M5.Display.width();
    int y = kSettingsHeaderHeight + visibleIndex * kSettingsRowHeight;
    return {0, static_cast<int16_t>(y), static_cast<int16_t>(displayW), kSettingsRowHeight};
}

int8_t SystemUIController::wifiIndexAt(int16_t x, int16_t y) const {
    (void)x;
    if (y < kSettingsHeaderHeight) return -1;
    uint8_t visibleIndex = (y - kSettingsHeaderHeight) / kSettingsRowHeight;
    if (visibleIndex >= visibleWifiRows()) return -1;
    uint8_t index = wifiScrollOffset_ + visibleIndex;
    if (index >= wifiItemCount()) return -1;
    return static_cast<int8_t>(index);
}

uint8_t SystemUIController::visibleToolRows() const {
    int rows = (M5.Display.height() - kSettingsHeaderHeight) / kSettingsRowHeight;
    if (rows < 1) return 1;
    return static_cast<uint8_t>(rows);
}

uint8_t SystemUIController::toolCategoryCount() const {
    return avatar_ ? avatar_->tools().categoryCount() : 0;
}

uint8_t SystemUIController::toolItemCount() const {
    if (!avatar_) return 0;
    if (toolView_ == ToolView::Categories) return avatar_->tools().categoryCount();
    return avatar_->tools().toolCount(toolCategorySelected_);
}

UiRect SystemUIController::toolItemBounds(uint8_t visibleIndex) const {
    int displayW = M5.Display.width();
    int y = kSettingsHeaderHeight + visibleIndex * kSettingsRowHeight;
    return {0, static_cast<int16_t>(y), static_cast<int16_t>(displayW), kSettingsRowHeight};
}

int8_t SystemUIController::toolIndexAt(int16_t x, int16_t y, bool categories) const {
    (void)x;
    if (y < kSettingsHeaderHeight) return -1;
    uint8_t visibleIndex = (y - kSettingsHeaderHeight) / kSettingsRowHeight;
    if (visibleIndex >= visibleToolRows()) return -1;

    uint8_t offset = categories ? toolCategoryScrollOffset_ : toolScrollOffset_;
    uint8_t total = categories ? toolCategoryCount()
                               : (avatar_ ? avatar_->tools().toolCount(toolCategorySelected_) : 0);
    uint8_t index = offset + visibleIndex;
    if (index >= total) return -1;
    return static_cast<int8_t>(index);
}

void SystemUIController::scrollSettings(int8_t delta) {
    uint8_t total = settingsItemCount();
    uint8_t visible = visibleSettingsRows();
    int maxOffset = total > visible ? total - visible : 0;
    int offset = settingsScrollOffset_ + delta;
    if (offset < 0) offset = 0;
    if (offset > maxOffset) offset = maxOffset;
    if (settingsScrollOffset_ == offset) return;
    settingsScrollOffset_ = offset;
    avatar_->display().setDirty();
}

void SystemUIController::scrollWifi(int8_t delta) {
    uint8_t total = wifiItemCount();
    uint8_t visible = visibleWifiRows();
    int maxOffset = total > visible ? total - visible : 0;
    int offset = wifiScrollOffset_ + delta;
    if (offset < 0) offset = 0;
    if (offset > maxOffset) offset = maxOffset;
    if (wifiScrollOffset_ == offset) return;
    wifiScrollOffset_ = offset;
    avatar_->display().setDirty();
}

void SystemUIController::scrollTools(int8_t delta) {
    uint8_t total = toolItemCount();
    uint8_t visible = visibleToolRows();
    int maxOffset = total > visible ? total - visible : 0;
    int offset = (toolView_ == ToolView::Categories ? toolCategoryScrollOffset_ : toolScrollOffset_) +
                 delta;
    if (offset < 0) offset = 0;
    if (offset > maxOffset) offset = maxOffset;

    if (toolView_ == ToolView::Categories) {
        if (toolCategoryScrollOffset_ == offset) return;
        toolCategoryScrollOffset_ = offset;
    } else {
        if (toolScrollOffset_ == offset) return;
        toolScrollOffset_ = offset;
    }
    avatar_->display().setDirty();
}

const char* SystemUIController::settingsTitle() const {
    switch (settingsView_) {
        case SettingsView::Root:
            return "設定";
        case SettingsView::Updates:
            return "アップデート";
        case SettingsView::Brightness:
            return "ライト";
        case SettingsView::Speaker:
            return "スピーカー";
        case SettingsView::WiFi:
            return "Wi-Fi";
        case SettingsView::IdleMotion:
            return "待機モーション";
        case SettingsView::Version:
            return "ファームウェア";
        case SettingsView::ToolUpdate:
            return "ツール設定";
    }
    return "設定";
}

const char* SystemUIController::toolTitle() const {
    if (toolView_ == ToolView::Tools && avatar_) {
        const char* name = avatar_->tools().categoryName(toolCategorySelected_);
        if (name && name[0]) return name;
    }
    return "ツール";
}

UiRect SystemUIController::otaCheckButtonBounds() const {
    int displayW = M5.Display.width();
    int y = M5.Display.height() - 80;
    return {18, static_cast<int16_t>(y), static_cast<int16_t>(displayW - 36), 32};
}

UiRect SystemUIController::otaUpdateButtonBounds() const {
    int displayW = M5.Display.width();
    int y = M5.Display.height() - 38;
    return {18, static_cast<int16_t>(y), static_cast<int16_t>(displayW - 36), 32};
}

void SystemUIController::drawOtaActionButton(LGFX_Sprite* canvas, UiRect bounds,
                                             const char* label, bool enabled) const {
    uint16_t fill = enabled ? 0x03E0 : 0x2104;
    uint16_t outline = enabled ? TFT_GREEN : 0x528A;
    uint16_t text = enabled ? TFT_BLACK : 0x8410;
    canvas->fillRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, fill);
    canvas->drawRoundRect(bounds.x, bounds.y, bounds.w, bounds.h, 8, outline);
    canvas->setFont(&fonts::lgfxJapanGothic_16);
    canvas->setTextSize(1);
    canvas->setTextDatum(middle_center);
    canvas->setTextColor(text);
    canvas->drawString(label, bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
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
