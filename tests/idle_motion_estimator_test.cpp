#include "../src/IdleMotionEstimator.h"

#include <cassert>
#include <cstdint>

using namespace aiavatar;

namespace {

void addConstantFrame(IdleAudioAccumulator& accumulator, int16_t left, int16_t right) {
    int16_t frame[128 * 2];
    for (int i = 0; i < 128; ++i) {
        frame[i * 2] = left;
        frame[i * 2 + 1] = right;
    }
    assert(addIdleStereoFrame(accumulator, frame, 128, -40));
}

}  // namespace

int main() {
    assert(clampIdleMotionIntervalSeconds(-1) == 1);
    assert(clampIdleMotionIntervalSeconds(0) == 1);
    assert(clampIdleMotionIntervalSeconds(5) == 5);
    assert(clampIdleMotionIntervalSeconds(31) == 30);

    assert(clampIdleMotionYaw(-999) == kIdleMotionYawMin);
    assert(clampIdleMotionYaw(999) == kIdleMotionYawMax);
    assert(clampIdleMotionPitch(200, -999) == 140);
    assert(clampIdleMotionPitch(200, 999) == 360);

    uint32_t cooldown = 0;
    assert(idleMotionCooldownReady(1000, cooldown));
    assert(cooldown == 0);
    scheduleNextIdleMotion(1000, 5, false, 0, cooldown);
    assert(cooldown == 6000);
    assert(!idleMotionCooldownReady(5999, cooldown));
    assert(cooldown == 6000);
    assert(idleMotionCooldownReady(6000, cooldown));
    assert(cooldown == 0);
    scheduleNextIdleMotion(1000, 99, false, 0, cooldown);
    assert(cooldown == 31000);
    scheduleNextIdleMotion(1000, 5, true, 0, cooldown);
    assert(cooldown == 1000);
    scheduleNextIdleMotion(1000, 5, true, 5000, cooldown);
    assert(cooldown == 6000);
    scheduleNextIdleMotion(1000, 5, true, 5001, cooldown);
    assert(cooldown == 1000);

    IdleAudioAccumulator silence = {};
    silence.reset();
    int16_t silentFrame[64 * 2] = {};
    assert(!addIdleStereoFrame(silence, silentFrame, 64, -40));
    IdleMotionTarget silentTarget = estimateIdleMotionTarget(silence, 200);
    assert(!silentTarget.active);

    IdleAudioAccumulator left = {};
    left.reset();
    addConstantFrame(left, 12000, 1000);
    IdleMotionTarget leftTarget = estimateIdleMotionTarget(left, 200);
    assert(leftTarget.active);
    assert(leftTarget.yaw < 0);
    assert(leftTarget.yaw >= kIdleMotionYawMin);
    assert(leftTarget.pitch == 200);

    IdleAudioAccumulator right = {};
    right.reset();
    addConstantFrame(right, 1000, 12000);
    IdleMotionTarget rightTarget = estimateIdleMotionTarget(right, 200);
    assert(rightTarget.active);
    assert(rightTarget.yaw > 0);
    assert(rightTarget.yaw <= kIdleMotionYawMax);
    assert(rightTarget.pitch == 200);

    IdleAudioAccumulator center = {};
    center.reset();
    addConstantFrame(center, 8000, 8000);
    IdleMotionTarget centerTarget = estimateIdleMotionTarget(center, 200);
    assert(centerTarget.active);
    assert(centerTarget.yaw == 0);
    assert(centerTarget.pitch == 200);

    IdleAudioAccumulator weakRight = {};
    weakRight.reset();
    addConstantFrame(weakRight, 8000, 10000);
    IdleMotionTarget weakRightTarget = estimateIdleMotionTarget(weakRight, 200);
    assert(weakRightTarget.active);
    assert(weakRightTarget.yaw > 0);
    assert(weakRightTarget.yaw < rightTarget.yaw);
    assert(weakRightTarget.pitch == 200);

    IdleAudioAccumulator subtleRight = {};
    subtleRight.reset();
    addConstantFrame(subtleRight, 9900, 10000);
    IdleMotionTarget subtleRightTarget = estimateIdleMotionTarget(subtleRight, 200);
    assert(subtleRightTarget.active);
    assert(subtleRightTarget.yaw >= 35);
    assert(subtleRightTarget.yaw < weakRightTarget.yaw);
    assert(subtleRightTarget.pitch == 200);

    IdleMotionTarget randomTarget = randomIdleMotionTarget(200, 0);
    assert(randomTarget.active);
    assert(randomTarget.yaw >= kIdleMotionYawMin && randomTarget.yaw <= kIdleMotionYawMax);
    assert(randomTarget.pitch >= 140 && randomTarget.pitch <= 360);

    randomTarget = randomIdleMotionTarget(200, 0xffffffff);
    assert(randomTarget.active);
    assert(randomTarget.yaw >= kIdleMotionYawMin && randomTarget.yaw <= kIdleMotionYawMax);
    assert(randomTarget.pitch >= 140 && randomTarget.pitch <= 360);

    return 0;
}
