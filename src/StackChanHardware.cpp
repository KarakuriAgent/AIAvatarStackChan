#include "StackChanHardware.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <cstring>
#include <strings.h>

#if __has_include(<M5StackChan.h>)
#include <M5StackChan.h>
#define AIAVATAR_HAS_M5STACKCHAN 1
#else
#define AIAVATAR_HAS_M5STACKCHAN 0
#endif

namespace aiavatar {

namespace {

static constexpr uint8_t kStackChanIoExpanderAddress = 0x6F;
static constexpr uint8_t kStackChanVersionRegister = 0x02;
static constexpr uint8_t kScsWriteInstruction = 0x03;
static constexpr uint8_t kScsGoalPositionAddress = 42;
static constexpr uint32_t kScsBaud = 1000000;
static constexpr int16_t kScsMinDegree = 0;
static constexpr int16_t kScsMaxDegree = 300;
static constexpr int16_t kScsHomeDegree = 150;
static constexpr int16_t kScsYawLimitDegree = 60;
static constexpr int16_t kScsPitchUpLimitDegree = 30;
static constexpr int16_t kScsPitchDownLimitDegree = 20;

#if AIAVATAR_HAS_M5STACKCHAN
bool detectStackChanHardware() {
    uint8_t version = M5.In_I2C.readRegister8(kStackChanIoExpanderAddress,
                                              kStackChanVersionRegister, 100000);
    return version != 0 && version != 0xFF;
}
#endif

bool isDirectScsType(const char* servoType) {
    return strcasecmp(servoType, "SCS") == 0 ||
           strcasecmp(servoType, "SCS0009") == 0;
}

}  // namespace

StackChanHardware::StackChanHardware()
    : active_(false),
      autoAngleSyncEnabled_(false),
      directScs_(false),
      servoIdX_(1),
      servoIdY_(2),
      pitchHome_(200) {}

bool StackChanHardware::begin() {
#if AIAVATAR_HAS_M5STACKCHAN
    if (active_) return true;
    if (!detectStackChanHardware()) {
        Serial.println("[StackChan] hardware not detected");
        return false;
    }
    M5StackChan.begin();
    // Avoid sudden jumps to the servo minimum when a transient position read fails.
    // This can be enabled again from Config for smoother motion if the hardware
    // reads servo positions reliably.
    M5StackChan.Motion.setAutoAngleSyncEnabled(autoAngleSyncEnabled_);
    active_ = true;
    directScs_ = false;
    Serial.printf("[StackChan] hardware initialized autoAngleSync=%d\n",
                  autoAngleSyncEnabled_ ? 1 : 0);
    return true;
#else
    active_ = false;
    Serial.println("[StackChan] M5StackChan.h is not available");
    return false;
#endif
}

bool StackChanHardware::begin(const Config& config) {
    if (isDirectScsType(config.servoType)) {
        return beginDirectScs(config);
    }
    return begin();
}

bool StackChanHardware::beginDirectScs(const Config& config) {
    if (active_ && directScs_) return true;

    servoIdX_ = config.servoIdX;
    servoIdY_ = config.servoIdY;
    pitchHome_ = config.pitchHome;

    // AI_StackChan_Ex's takao_base=true path disables CoreS3 EXT output because
    // the Takao base supplies servo power from the base side.
    M5.Power.setExtOutput(!config.takaoBase);
    Serial2.begin(kScsBaud, SERIAL_8N1, config.servoRxPin, config.servoTxPin);

    active_ = true;
    directScs_ = true;
    Serial.printf("[StackChan] direct SCS initialized rx=%u tx=%u idX=%u idY=%u takaoBase=%d\n",
                  config.servoRxPin, config.servoTxPin, servoIdX_, servoIdY_,
                  config.takaoBase ? 1 : 0);
    return true;
}

void StackChanHardware::setAutoAngleSyncEnabled(bool enabled) {
    autoAngleSyncEnabled_ = enabled;
#if AIAVATAR_HAS_M5STACKCHAN
    if (active_ && !directScs_) {
        M5StackChan.Motion.setAutoAngleSyncEnabled(enabled);
        Serial.printf("[StackChan] autoAngleSync=%d\n", enabled ? 1 : 0);
    }
#else
    (void)enabled;
#endif
}

void StackChanHardware::update() {
    if (active_ && directScs_) {
        M5.update();
        return;
    }
#if AIAVATAR_HAS_M5STACKCHAN
    if (active_ && !directScs_) M5StackChan.update();
#endif
}

void StackChanHardware::moveMotion(int16_t yaw, int16_t pitch, uint16_t speed) {
    if (!active_) return;
    if (directScs_) {
        int16_t yawDegree = kScsHomeDegree + yaw / 6;
        yawDegree = constrain(yawDegree, kScsHomeDegree - kScsYawLimitDegree,
                              kScsHomeDegree + kScsYawLimitDegree);

        int16_t pitchDegree = kScsHomeDegree + (pitch - pitchHome_) / 6;
        pitchDegree = constrain(pitchDegree, kScsHomeDegree - kScsPitchUpLimitDegree,
                                kScsHomeDegree + kScsPitchDownLimitDegree);

        uint16_t moveTimeMs = speed > 0 ? speed : 300;
        writeScsPosition(servoIdX_, degreeToScsPosition(yawDegree), moveTimeMs, 0);
        writeScsPosition(servoIdY_, degreeToScsPosition(pitchDegree), moveTimeMs, 0);
        return;
    }
#if AIAVATAR_HAS_M5STACKCHAN
    M5StackChan.Motion.move(yaw, pitch, speed);
#else
    (void)yaw;
    (void)pitch;
    (void)speed;
#endif
}

bool StackChanHardware::consumeNadeEvent() {
#if AIAVATAR_HAS_M5STACKCHAN
    if (!active_ || directScs_) return false;
    if (M5StackChan.TouchSensor.wasSwipedForward()) {
        Serial.println("[StackChan] nade by swipe forward");
        return true;
    }
    if (M5StackChan.TouchSensor.wasSwipedBackward()) {
        Serial.println("[StackChan] nade by swipe backward");
        return true;
    }
    if (M5StackChan.TouchSensor.wasClicked()) {
        Serial.println("[StackChan] nade by click");
        return true;
    }
#endif
    return false;
}

void StackChanHardware::setLedColor(uint8_t r, uint8_t g, uint8_t b) {
#if AIAVATAR_HAS_M5STACKCHAN
    if (!active_ || directScs_) return;
    for (uint8_t i = 0; i < 12; ++i) {
        M5StackChan.setRgbColor(i, r, g, b);
    }
    M5StackChan.refreshRgb();
#else
    (void)r;
    (void)g;
    (void)b;
#endif
}

void StackChanHardware::setLedPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
#if AIAVATAR_HAS_M5STACKCHAN
    if (!active_ || directScs_ || index >= 12) return;
    M5StackChan.setRgbColor(index, r, g, b);
#else
    (void)index;
    (void)r;
    (void)g;
    (void)b;
#endif
}

void StackChanHardware::refreshLed() {
#if AIAVATAR_HAS_M5STACKCHAN
    if (active_ && !directScs_) M5StackChan.refreshRgb();
#endif
}

void StackChanHardware::writeScsPosition(uint8_t id, uint16_t position, uint16_t timeMs,
                                         uint16_t speed) {
    uint8_t packet[13] = {
        0xFF,
        0xFF,
        id,
        9,
        kScsWriteInstruction,
        kScsGoalPositionAddress,
        static_cast<uint8_t>((position >> 8) & 0xFF),
        static_cast<uint8_t>(position & 0xFF),
        static_cast<uint8_t>((timeMs >> 8) & 0xFF),
        static_cast<uint8_t>(timeMs & 0xFF),
        static_cast<uint8_t>((speed >> 8) & 0xFF),
        static_cast<uint8_t>(speed & 0xFF),
        0,
    };

    uint16_t sum = 0;
    for (uint8_t i = 2; i < 12; ++i) {
        sum += packet[i];
    }
    packet[12] = static_cast<uint8_t>(~sum);
    Serial2.write(packet, sizeof(packet));
    Serial2.flush();
}

uint16_t StackChanHardware::degreeToScsPosition(int16_t degree) const {
    degree = constrain(degree, kScsMinDegree, kScsMaxDegree);
    return map(degree, kScsMinDegree, kScsMaxDegree, 0, 1023);
}

}  // namespace aiavatar
