#include "SpiBusLock.h"

namespace aiavatar {

namespace {

SemaphoreHandle_t busMutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
    return mutex;
}

}  // namespace

void SpiBusLock::lock() {
    xSemaphoreTakeRecursive(busMutex(), portMAX_DELAY);
}

void SpiBusLock::unlock() {
    xSemaphoreGiveRecursive(busMutex());
}

}  // namespace aiavatar
