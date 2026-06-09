#include "MotionController.h"

#include <Arduino.h>

namespace aiavatar {

namespace {

const MotionKeyframe kNadeFurifuri[] = {
    {-150, 450, 400, 400},
    {150, 450, 400, 400},
    {-120, 450, 400, 350},
    {120, 450, 400, 350},
};

const MotionKeyframe kNadeCircle[] = {
    {-100, 300, 300, 400},
    {100, 300, 300, 400},
    {100, 600, 300, 400},
    {-100, 600, 300, 400},
};

const MotionKeyframe kNadePurupuru[] = {
    {0, 600, 400, 200},
    {-60, 600, 800, 100},
    {60, 600, 800, 100},
    {-60, 600, 800, 100},
    {60, 600, 800, 100},
    {-60, 600, 800, 100},
    {60, 600, 800, 100},
};

const MotionKeyframe kNadeNod[] = {
    {0, 300, 400, 350},
    {0, 600, 400, 350},
    {0, 300, 400, 350},
    {0, 600, 400, 350},
};

const MotionKeyframe kNadeDiagonal[] = {
    {-120, 300, 350, 400},
    {120, 550, 350, 400},
    {-120, 300, 350, 400},
    {120, 550, 350, 400},
};

const MotionSequence kDefaultNadeMotions[] = {
    {kNadeFurifuri, static_cast<uint8_t>(sizeof(kNadeFurifuri) / sizeof(MotionKeyframe))},
    {kNadeCircle, static_cast<uint8_t>(sizeof(kNadeCircle) / sizeof(MotionKeyframe))},
    {kNadePurupuru, static_cast<uint8_t>(sizeof(kNadePurupuru) / sizeof(MotionKeyframe))},
    {kNadeNod, static_cast<uint8_t>(sizeof(kNadeNod) / sizeof(MotionKeyframe))},
    {kNadeDiagonal, static_cast<uint8_t>(sizeof(kNadeDiagonal) / sizeof(MotionKeyframe))},
};

}  // namespace

MotionController::MotionController()
    : enabled_(false),
      hardware_(nullptr),
      pitchHome_(200),
      nadeCb_(nullptr),
      nadeMotions_(kDefaultNadeMotions),
      nadeMotionCount_(sizeof(kDefaultNadeMotions) / sizeof(MotionSequence)),
      nadeActive_(false),
      nadeMotionIdx_(0),
      nadeStepIdx_(0),
      nadeStepStartMs_(0),
      nadeCooldownUntil_(0),
      nadePendingCb_(false),
      nextMoveMs_(0),
      wasSpeaking_(false),
      lastYaw_(0),
      lastPitch_(0),
      speakStopMs_(0),
      pendingStop_(false) {}

void MotionController::begin(int16_t pitchHome) {
    pitchHome_ = pitchHome;
    enabled_ = hardware_ && hardware_->motionAvailable();
    if (enabled_) goHome(500);
}

bool MotionController::updateHardware() {
    if (hardware_) {
        hardware_->update();
        return true;
    }
    return false;
}

void MotionController::update(bool speaking) {
    pollNadeSensor();
    updateNadeMotion();
    updateSpeakingMotion(speaking);
    firePendingNadeCallback();
}

void MotionController::setHardware(HardwareAdapter* hardware) {
    hardware_ = hardware;
    enabled_ = hardware_ && hardware_->motionAvailable();
    if (enabled_) {
        goHome(500);
        Serial.printf("[Motion] hardware=%s pitchHome=%d\n", hardware_->name(), pitchHome_);
    }
}

void MotionController::move(int16_t yaw, int16_t pitch, uint16_t speed) {
    if (!enabled_ || !hardware_) return;
    hardware_->moveMotion(yaw, pitch, speed);
}

bool MotionController::moveIdleTarget(int16_t yaw, int16_t pitch, uint16_t speed) {
    if (!enabled_ || !hardware_ || nadeActive_) return false;
    yaw = clampIdleMotionYaw(yaw);
    pitch = clampIdleMotionPitch(pitchHome_, pitch);
    pendingStop_ = false;
    lastYaw_ = yaw;
    lastPitch_ = static_cast<int16_t>(pitch - pitchHome_);
    move(yaw, pitch, speed);
    return true;
}

void MotionController::goHome(uint16_t speed) {
    move(0, pitchHome_, speed);
}

void MotionController::setNadeMotions(const MotionSequence* motions, uint8_t motionCount) {
    if (!motions || motionCount == 0) {
        useDefaultNadeMotions();
        return;
    }
    nadeMotions_ = motions;
    nadeMotionCount_ = motionCount;
}

void MotionController::useDefaultNadeMotions() {
    nadeMotions_ = kDefaultNadeMotions;
    nadeMotionCount_ = sizeof(kDefaultNadeMotions) / sizeof(MotionSequence);
}

bool MotionController::triggerNadeMotion() {
    if (!enabled_ || nadeActive_ || millis() < nadeCooldownUntil_ ||
        !nadeMotions_ || nadeMotionCount_ == 0) {
        return false;
    }

    nadeMotionIdx_ = random(0, nadeMotionCount_);
    const MotionSequence& motion = nadeMotions_[nadeMotionIdx_];
    if (!motion.frames || motion.frameCount == 0) return false;

    nadeStepIdx_ = 0;
    nadeActive_ = true;
    nadePendingCb_ = true;

    int16_t pitchOffset = pitchHome_ - 450;
    const MotionKeyframe& frame = motion.frames[0];
    move(frame.yaw, frame.pitch + pitchOffset, frame.speed);
    nadeStepStartMs_ = millis();
    Serial.printf("[Motion] nade motion %u started\n", nadeMotionIdx_);
    return true;
}

void MotionController::pollNadeSensor() {
    if (!enabled_ || !hardware_) return;
    if (hardware_->consumeNadeEvent()) {
        triggerNadeMotion();
    }
}

void MotionController::updateNadeMotion() {
    if (!enabled_ || !nadeActive_) return;

    const MotionSequence& motion = nadeMotions_[nadeMotionIdx_];
    if (millis() - nadeStepStartMs_ < motion.frames[nadeStepIdx_].holdMs) return;

    ++nadeStepIdx_;
    if (nadeStepIdx_ >= motion.frameCount) {
        nadeActive_ = false;
        nadeCooldownUntil_ = millis() + kNadeCooldownMs;
        goHome(300);
        Serial.println("[Motion] nade motion complete");
        return;
    }

    int16_t pitchOffset = pitchHome_ - 450;
    const MotionKeyframe& frame = motion.frames[nadeStepIdx_];
    move(frame.yaw, frame.pitch + pitchOffset, frame.speed);
    nadeStepStartMs_ = millis();
}

void MotionController::updateSpeakingMotion(bool speaking) {
    if (!enabled_ || nadeActive_) {
        wasSpeaking_ = speaking;
        return;
    }

    if (speaking) {
        pendingStop_ = false;
        if (millis() >= nextMoveMs_) {
            int16_t targetYaw = random(-200, 201);
            int16_t targetPitch = random(-100, 301);
            int16_t yaw = constrain(targetYaw, lastYaw_ - 80, lastYaw_ + 80);
            int16_t pitch = constrain(targetPitch, lastPitch_ - 100, lastPitch_ + 100);
            lastYaw_ = yaw;
            lastPitch_ = pitch;
            move(yaw, pitchHome_ + pitch, 200);
            nextMoveMs_ = millis() + random(500, 1001);
        }
    } else if (wasSpeaking_ && !pendingStop_) {
        pendingStop_ = true;
        speakStopMs_ = millis();
    } else if (pendingStop_ && millis() - speakStopMs_ >= kSpeakStopDelayMs) {
        pendingStop_ = false;
        lastYaw_ = 0;
        lastPitch_ = 0;
        goHome();
    }

    wasSpeaking_ = speaking;
}

void MotionController::firePendingNadeCallback() {
    if (!nadePendingCb_) return;
    nadePendingCb_ = false;
    if (nadeCb_) nadeCb_();
}

}  // namespace aiavatar
