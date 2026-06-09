#pragma once

#include <M5Unified.h>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace aiavatar {

class MicrophoneInput {
public:
    MicrophoneInput();

    void configure(uint32_t sampleRate, uint8_t magnification, size_t bufferSamples);
    bool beginQueue(size_t depth = 2);
    bool begin();
    void end();
    bool read(int16_t* dest, size_t sampleCount);
    bool readStereo(int16_t* dest, size_t frameCount);
    bool enqueueFrame(const int16_t* samples);
    bool dequeueFrame(int16_t* dest);
    void clearQueue();

    static void downmixStereoToMono(const int16_t* stereoSamples, int16_t* monoSamples,
                                    size_t frameCount);

    uint32_t sampleRate() const { return sampleRate_; }
    size_t bufferSamples() const { return bufferSamples_; }
    bool isRunning() const { return running_; }

private:
    uint32_t sampleRate_;
    uint8_t magnification_;
    size_t bufferSamples_;
    bool running_;
    QueueHandle_t queue_;
    int16_t* dropBuf_;
};

}  // namespace aiavatar
