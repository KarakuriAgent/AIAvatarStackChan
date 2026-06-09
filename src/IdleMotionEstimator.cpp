#include "IdleMotionEstimator.h"

#include <cmath>

namespace aiavatar {

namespace {

static constexpr float kIdleMotionYawDeadZone = 0.003f;
static constexpr float kIdleMotionYawGain = 8.0f;
static constexpr int16_t kIdleMotionMinVisibleYaw = 35;

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

int16_t roundToInt16(float value) {
    return static_cast<int16_t>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

int64_t thresholdEnergyPerSample(int8_t thresholdDb) {
    float threshold = 32768.0f * powf(10.0f, static_cast<float>(thresholdDb) / 20.0f);
    return static_cast<int64_t>(threshold * threshold);
}

}  // namespace

void IdleAudioAccumulator::reset() {
    leftEnergy = 0;
    rightEnergy = 0;
    activeSamples = 0;
}

uint8_t clampIdleMotionIntervalSeconds(int seconds) {
    if (seconds < kIdleMotionIntervalMinSeconds) return kIdleMotionIntervalMinSeconds;
    if (seconds > kIdleMotionIntervalMaxSeconds) return kIdleMotionIntervalMaxSeconds;
    return static_cast<uint8_t>(seconds);
}

int16_t clampIdleMotionYaw(int32_t yaw) {
    if (yaw < kIdleMotionYawMin) return kIdleMotionYawMin;
    if (yaw > kIdleMotionYawMax) return kIdleMotionYawMax;
    return static_cast<int16_t>(yaw);
}

int16_t clampIdleMotionPitch(int16_t pitchHome, int32_t pitch) {
    int32_t minPitch = static_cast<int32_t>(pitchHome) + kIdleMotionRandomPitchUpOffset;
    int32_t maxPitch = static_cast<int32_t>(pitchHome) + kIdleMotionRandomPitchDownOffset;
    if (pitch < minPitch) return static_cast<int16_t>(minPitch);
    if (pitch > maxPitch) return static_cast<int16_t>(maxPitch);
    return static_cast<int16_t>(pitch);
}

bool idleMotionCooldownReady(uint32_t nowMs, uint32_t& cooldownUntilMs) {
    if (cooldownUntilMs == 0) return true;
    if (static_cast<int32_t>(nowMs - cooldownUntilMs) < 0) return false;
    cooldownUntilMs = 0;
    return true;
}

void scheduleNextIdleMotion(uint32_t nowMs, uint8_t intervalSeconds,
                            bool randomize, uint32_t randomValue,
                            uint32_t& nextMotionMs) {
    uint32_t maxDelayMs =
        static_cast<uint32_t>(clampIdleMotionIntervalSeconds(intervalSeconds)) * 1000UL;
    uint32_t delayMs = maxDelayMs;
    if (randomize && maxDelayMs > 0) {
        delayMs = randomValue % (maxDelayMs + 1);
    }
    nextMotionMs = nowMs + delayMs;
}

bool addIdleStereoFrame(IdleAudioAccumulator& accumulator, const int16_t* stereoSamples,
                        size_t frameCount, int8_t thresholdDb) {
    if (!stereoSamples || frameCount == 0) return false;

    int64_t frameStereoEnergy = 0;
    for (size_t i = 0; i < frameCount; ++i) {
        int32_t left = stereoSamples[i * 2];
        int32_t right = stereoSamples[i * 2 + 1];
        frameStereoEnergy += static_cast<int64_t>(left) * left;
        frameStereoEnergy += static_cast<int64_t>(right) * right;
    }

    if (frameStereoEnergy <= thresholdEnergyPerSample(thresholdDb) *
                                static_cast<int64_t>(frameCount) * 2) {
        return false;
    }

    for (size_t i = 0; i < frameCount; ++i) {
        int32_t left = stereoSamples[i * 2];
        int32_t right = stereoSamples[i * 2 + 1];

        accumulator.leftEnergy += static_cast<int64_t>(left) * left;
        accumulator.rightEnergy += static_cast<int64_t>(right) * right;
    }
    accumulator.activeSamples += frameCount;
    return true;
}

IdleMotionTarget estimateIdleMotionTarget(const IdleAudioAccumulator& accumulator,
                                          int16_t pitchHome) {
    IdleMotionTarget target = {
        false,
        0,
        pitchHome,
    };
    if (accumulator.activeSamples == 0) return target;

    float left = sqrtf(static_cast<float>(accumulator.leftEnergy) /
                       static_cast<float>(accumulator.activeSamples));
    float right = sqrtf(static_cast<float>(accumulator.rightEnergy) /
                        static_cast<float>(accumulator.activeSamples));
    float total = left + right;
    if (total < 1.0f) return target;

    float balance = (right - left) / total;
    int16_t yaw = 0;
    if (fabsf(balance) > kIdleMotionYawDeadZone) {
        float sign = balance >= 0.0f ? 1.0f : -1.0f;
        float yawNorm = ((fabsf(balance) - kIdleMotionYawDeadZone) /
                         (1.0f - kIdleMotionYawDeadZone)) * kIdleMotionYawGain;
        yawNorm = clampFloat(yawNorm, 0.0f, 1.0f);
        yaw = roundToInt16(sign * yawNorm * kIdleMotionYawMax);
        if (yaw > 0 && yaw < kIdleMotionMinVisibleYaw) yaw = kIdleMotionMinVisibleYaw;
        if (yaw < 0 && yaw > -kIdleMotionMinVisibleYaw) yaw = -kIdleMotionMinVisibleYaw;
    }

    target.active = true;
    target.yaw = clampIdleMotionYaw(yaw);
    target.pitch = pitchHome;
    return target;
}

IdleMotionTarget randomIdleMotionTarget(int16_t pitchHome, uint32_t randomValue) {
    static constexpr uint32_t kYawSpan =
        static_cast<uint32_t>(kIdleMotionRandomYawMax - kIdleMotionRandomYawMin + 1);
    static constexpr uint32_t kPitchSpan =
        static_cast<uint32_t>(kIdleMotionRandomPitchDownOffset -
                              kIdleMotionRandomPitchUpOffset + 1);

    int16_t yaw = static_cast<int16_t>(kIdleMotionRandomYawMin +
                                      static_cast<int16_t>(randomValue % kYawSpan));
    uint32_t pitchSeed = randomValue / kYawSpan;
    int16_t pitchOffset =
        static_cast<int16_t>(kIdleMotionRandomPitchUpOffset +
                             static_cast<int16_t>(pitchSeed % kPitchSpan));

    IdleMotionTarget target = {
        true,
        clampIdleMotionYaw(yaw),
        clampIdleMotionPitch(pitchHome, static_cast<int32_t>(pitchHome) + pitchOffset),
    };
    return target;
}

}  // namespace aiavatar
