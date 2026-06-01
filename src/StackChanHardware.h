#pragma once

#include "HardwareAdapter.h"
#include "Config.h"

namespace aiavatar {

class StackChanHardware : public HardwareAdapter {
public:
    StackChanHardware();

    bool begin() override;
    bool begin(const Config& config);
    void update() override;
    const char* name() const override { return "StackChan"; }

    bool motionAvailable() const override { return active_; }
    void moveMotion(int16_t yaw, int16_t pitch, uint16_t speed) override;
    bool consumeNadeEvent() override;
    bool ledAvailable() const override { return active_ && !directScs_; }
    uint8_t ledCount() const override { return ledAvailable() ? 12 : 0; }
    void setLedColor(uint8_t r, uint8_t g, uint8_t b) override;
    void setLedPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b) override;
    void refreshLed() override;

    bool active() const { return active_; }
    void setAutoAngleSyncEnabled(bool enabled);
    bool autoAngleSyncEnabled() const { return autoAngleSyncEnabled_; }

private:
    bool active_;
    bool autoAngleSyncEnabled_;
    bool directScs_;
    uint8_t servoIdX_;
    uint8_t servoIdY_;
    int16_t pitchHome_;
    int16_t servoYawOffsetDegree_;

    bool beginDirectScs(const Config& config);
    void writeScsPosition(uint8_t id, uint16_t position, uint16_t timeMs, uint16_t speed);
    uint16_t degreeToScsPosition(int16_t degree) const;
};

}  // namespace aiavatar
