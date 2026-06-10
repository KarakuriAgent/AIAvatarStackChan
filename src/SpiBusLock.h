#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace aiavatar {

// CoreS3はLCDとSDカードがSPIバスを共有し、LCDのD/CとSDのMISOがGPIO35を共有している。
// 別タスクから描画とSDアクセスを高頻度で並行させるとSDカードがコマンド無応答になるため、
// LCDへのpushSpriteとSDカードへの連続アクセスは必ずこのロックで排他すること。
// 再帰ロック可(同一タスク内のネストは安全)。
class SpiBusLock {
public:
    static void lock();
    static void unlock();
};

class SpiBusLockGuard {
public:
    SpiBusLockGuard() { SpiBusLock::lock(); }
    ~SpiBusLockGuard() { SpiBusLock::unlock(); }
    SpiBusLockGuard(const SpiBusLockGuard&) = delete;
    SpiBusLockGuard& operator=(const SpiBusLockGuard&) = delete;
};

}  // namespace aiavatar
