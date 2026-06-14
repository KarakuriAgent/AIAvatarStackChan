#pragma once

#include <Arduino.h>

namespace aiavatar {

struct Config;

class CameraController {
public:
    bool begin();
    bool begin(const Config& config);
    bool configure(const Config& config);
    void update();
    bool isReady() const { return ready_; }
    bool captureJpeg(uint8_t** outBuf, size_t* outLen, uint8_t quality = 80);

private:
    bool ready_ = false;

#if defined(AIAVATAR_BOARD_ATOMS3)
    bool remoteI2cStarted_ = false;
    bool remoteConfigBuilt_ = false;
    String remoteConfigJson_;
    String remoteAuthToken_;
    String remoteIp_;
    int remoteState_ = -1;
    uint8_t remoteConfigGeneration_ = 0;
    uint32_t remoteLastConfigSentMs_ = 0;
    uint32_t remoteLastStatusMs_ = 0;

    bool beginRemoteI2c();
    bool buildRemoteConfig(const Config& config);
    bool sendRemoteConfig(bool force = false);
    bool pollRemoteStatus();
    bool captureRemoteJpeg(uint8_t** outBuf, size_t* outLen);
#endif
};

}  // namespace aiavatar
