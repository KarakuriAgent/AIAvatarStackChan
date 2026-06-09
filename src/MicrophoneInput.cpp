#include "MicrophoneInput.h"

#include <Arduino.h>
#include <cstdlib>

namespace aiavatar {

MicrophoneInput::MicrophoneInput()
    : sampleRate_(16000),
      magnification_(16),
      bufferSamples_(1024),
      running_(false),
      queue_(nullptr),
      dropBuf_(nullptr) {}

void MicrophoneInput::configure(uint32_t sampleRate, uint8_t magnification, size_t bufferSamples) {
    sampleRate_ = sampleRate;
    magnification_ = magnification;
    bufferSamples_ = bufferSamples;
}

bool MicrophoneInput::beginQueue(size_t depth) {
    if (bufferSamples_ == 0 || depth == 0) return false;
    if (!dropBuf_) {
        dropBuf_ = static_cast<int16_t*>(malloc(bufferSamples_ * sizeof(int16_t)));
        if (!dropBuf_) {
            Serial.println("[Mic] queue drop buffer allocation failed");
            return false;
        }
    }
    if (!queue_) {
        queue_ = xQueueCreate(depth, bufferSamples_ * sizeof(int16_t));
        if (!queue_) {
            Serial.println("[Mic] queue allocation failed");
            return false;
        }
    }
    return true;
}

bool MicrophoneInput::begin() {
    auto cfg = M5.Mic.config();
    cfg.sample_rate = sampleRate_;
    cfg.magnification = magnification_;
    M5.Mic.config(cfg);
    running_ = M5.Mic.begin();
    Serial.printf("[Mic] %s rate=%u mag=%u samples=%u\n",
                  running_ ? "started" : "failed", sampleRate_, magnification_, bufferSamples_);
    return running_;
}

void MicrophoneInput::end() {
    if (!running_) return;
    M5.Mic.end();
    running_ = false;
}

bool MicrophoneInput::read(int16_t* dest, size_t sampleCount) {
    if (!running_ || !dest || sampleCount == 0) return false;
    return M5.Mic.record(dest, sampleCount, sampleRate_);
}

bool MicrophoneInput::readStereo(int16_t* dest, size_t frameCount) {
    if (!running_ || !dest || frameCount == 0) return false;
    return M5.Mic.record(dest, frameCount * 2, sampleRate_, true);
}

void MicrophoneInput::downmixStereoToMono(const int16_t* stereoSamples, int16_t* monoSamples,
                                          size_t frameCount) {
    if (!stereoSamples || !monoSamples) return;
    for (size_t i = 0; i < frameCount; ++i) {
        int32_t left = stereoSamples[i * 2];
        int32_t right = stereoSamples[i * 2 + 1];
        monoSamples[i] = static_cast<int16_t>((left + right) / 2);
    }
}

bool MicrophoneInput::enqueueFrame(const int16_t* samples) {
    if (!queue_ || !dropBuf_ || !samples) return false;
    if (xQueueSend(queue_, samples, 0) == pdTRUE) return true;
    if (xQueueReceive(queue_, dropBuf_, 0) == pdTRUE) {
        return xQueueSend(queue_, samples, 0) == pdTRUE;
    }
    return false;
}

bool MicrophoneInput::dequeueFrame(int16_t* dest) {
    if (!queue_ || !dest) return false;
    return xQueueReceive(queue_, dest, 0) == pdTRUE;
}

void MicrophoneInput::clearQueue() {
    if (queue_) xQueueReset(queue_);
}

}  // namespace aiavatar
