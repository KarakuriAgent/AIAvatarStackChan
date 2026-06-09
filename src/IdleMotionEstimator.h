#pragma once

#include <cstddef>
#include <cstdint>

namespace aiavatar {

static constexpr int16_t kIdleMotionYawMin = -200;
static constexpr int16_t kIdleMotionYawMax = 200;
static constexpr int16_t kIdleMotionRandomYawMin = -180;
static constexpr int16_t kIdleMotionRandomYawMax = 180;
static constexpr int16_t kIdleMotionRandomPitchUpOffset = -60;
static constexpr int16_t kIdleMotionRandomPitchDownOffset = 160;
static constexpr uint8_t kIdleMotionIntervalMinSeconds = 1;
static constexpr uint8_t kIdleMotionIntervalMaxSeconds = 30;

struct IdleAudioAccumulator {
    int64_t leftEnergy;
    int64_t rightEnergy;
    size_t activeSamples;

    void reset();
};

struct IdleMotionTarget {
    bool active;
    int16_t yaw;
    int16_t pitch;
};

uint8_t clampIdleMotionIntervalSeconds(int seconds);
int16_t clampIdleMotionYaw(int32_t yaw);
int16_t clampIdleMotionPitch(int16_t pitchHome, int32_t pitch);
bool idleMotionCooldownReady(uint32_t nowMs, uint32_t& cooldownUntilMs);
void scheduleNextIdleMotion(uint32_t nowMs, uint8_t intervalSeconds,
                            bool randomize, uint32_t randomValue,
                            uint32_t& nextMotionMs);
bool addIdleStereoFrame(IdleAudioAccumulator& accumulator, const int16_t* stereoSamples,
                        size_t frameCount, int8_t thresholdDb);
IdleMotionTarget estimateIdleMotionTarget(const IdleAudioAccumulator& accumulator,
                                          int16_t pitchHome);
IdleMotionTarget randomIdleMotionTarget(int16_t pitchHome, uint32_t randomValue);

}  // namespace aiavatar
