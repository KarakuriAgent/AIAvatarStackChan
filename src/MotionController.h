#pragma once

#include "Config.h"
#include "HardwareAdapter.h"
#include "IdleMotionEstimator.h"

#include <cstddef>
#include <cstdint>

namespace aiavatar {

using NadeCallback = void (*)();

struct MotionKeyframe {
    int16_t yaw;
    int16_t pitch;
    uint16_t speed;
    uint16_t holdMs;
};

struct MotionSequence {
    const MotionKeyframe* frames;
    uint8_t frameCount;
};

class MotionController {
public:
    MotionController();

    void begin(int16_t pitchHome);
    bool updateHardware();
    void update(bool speaking);
    void setHardware(HardwareAdapter* hardware);

    bool triggerNadeMotion();
    bool isEnabled() const { return enabled_; }
    bool isNadeActive() const { return nadeActive_; }

    void move(int16_t yaw, int16_t pitch, uint16_t speed);
    bool moveIdleTarget(int16_t yaw, int16_t pitch, uint16_t speed = 450);
    void goHome(uint16_t speed = 500);

    int16_t pitchHome() const { return pitchHome_; }
    void setPitchHome(int16_t pitchHome) { pitchHome_ = pitchHome; }

    void setNadeMotions(const MotionSequence* motions, uint8_t motionCount);
    void useDefaultNadeMotions();
    void onNade(NadeCallback cb) { nadeCb_ = cb; }

private:
    bool enabled_;
    HardwareAdapter* hardware_;
    int16_t pitchHome_;
    NadeCallback nadeCb_;

    const MotionSequence* nadeMotions_;
    uint8_t nadeMotionCount_;
    bool nadeActive_;
    uint8_t nadeMotionIdx_;
    uint8_t nadeStepIdx_;
    uint32_t nadeStepStartMs_;
    uint32_t nadeCooldownUntil_;
    bool nadePendingCb_;

    uint32_t nextMoveMs_;
    bool wasSpeaking_;
    int16_t lastYaw_;
    int16_t lastPitch_;
    uint32_t speakStopMs_;
    bool pendingStop_;

    static constexpr uint32_t kSpeakStopDelayMs = 600;
    static constexpr uint32_t kNadeCooldownMs = 3000;

    void pollNadeSensor();
    void updateNadeMotion();
    void updateSpeakingMotion(bool speaking);
    void firePendingNadeCallback();
};

}  // namespace aiavatar
