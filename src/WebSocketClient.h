#pragma once

#include "AudioConverter.h"

#include <WebSocketsClient.h>
#include <cstddef>
#include <cstdint>

namespace aiavatar {

struct IncomingAudioChunk {
    const char* type;
    const char* codec;
    uint32_t sampleRate;
    uint8_t channels;
    uint8_t bitsPerSample;
    const int16_t* pcmData;
    size_t pcmSamples;
    const char* faceName;
    float faceDurationSec;
};

using ConnectionCallback = void (*)(bool connected);
using AudioChunkCallback = void (*)(const IncomingAudioChunk& chunk);
using TextCallback = void (*)(const char* text);
using FinalTextCallback = void (*)(const char* responseText, const char* voiceText);
using SimpleCallback = void (*)();
using ProcessingCallback = void (*)(bool processing);
using FaceCallback = void (*)(const char* faceName, float durationSec);
using ToolCallCallback = void (*)(const char* toolName);
using VisionCallback = void (*)();
using AudioFrameReadCallback = bool (*)(int16_t* dest, void* context);
using AudioFrameClearCallback = void (*)(void* context);

struct AudioFrameProvider {
    AudioFrameReadCallback read;
    AudioFrameClearCallback clear;
    void* context;
};

class WebSocketClient {
public:
    WebSocketClient();

    void onConnectionChange(ConnectionCallback cb) { connectionCb_ = cb; }
    void onAudioChunk(AudioChunkCallback cb) { audioChunkCb_ = cb; }
    void onStart(TextCallback cb) { startCb_ = cb; }
    void onStop(SimpleCallback cb) { stopCb_ = cb; }
    void onFinal(SimpleCallback cb) { finalCb_ = cb; }
    void onFinalText(FinalTextCallback cb) { finalTextCb_ = cb; }
    void onAccepted(SimpleCallback cb) { acceptedCb_ = cb; }
    void onServerSpeechDetected(SimpleCallback cb) { serverSpeechDetectedCb_ = cb; }
    void onProcessing(ProcessingCallback cb) { processingCb_ = cb; }
    void onFace(FaceCallback cb) { faceCb_ = cb; }
    void onToolCall(ToolCallCallback cb) { toolCallCb_ = cb; }
    void onVision(VisionCallback cb) { visionCb_ = cb; }

    bool configureAudioUpload(const AudioFrameProvider& provider, size_t frameSamples,
                              uint32_t slowBackoffMs,
                              uint32_t failBackoffMs, uint32_t keepaliveIntervalMs);
    void setUploadAudioConverter(AudioConverter* converter) { uploadConverter_ = converter; }
    void setPlaybackAudioConverter(AudioConverter* converter) { playbackConverter_ = converter; }
    void setUploadPcmFormat(uint32_t sampleRate, uint8_t channels);

    void begin(const char* host, uint16_t port, const char* path, const char* userId,
               uint32_t reconnectIntervalMs = 5000, const char* channel = nullptr);
    void reconnect(const char* host, uint16_t port, const char* path, const char* userId,
                   uint32_t reconnectIntervalMs = 5000, const char* channel = nullptr);
    void disconnect();
    void loop();

    bool sendAudioData(const int16_t* pcmData, size_t sampleCount, uint32_t* elapsedMs = nullptr);
    bool sendInvoke(const char* text);
    bool sendInvokeWithImage(const char* text, const char* imageDataUrl);
    bool reserveInvokeAudioBuffer(size_t sampleCount);
    bool sendInvokeWithAudio(const int16_t* pcmData, size_t sampleCount);
    void sendStop();

    bool isConnected() const { return connected_; }
    const char* sessionId() const { return sessionId_; }

private:
    WebSocketsClient ws_;
    volatile bool connected_;
    volatile bool autoReconnectEnabled_;
    char sessionId_[37];
    char userId_[64];
    char channel_[32];

    ConnectionCallback connectionCb_;
    AudioChunkCallback audioChunkCb_;
    TextCallback startCb_;
    SimpleCallback stopCb_;
    SimpleCallback finalCb_;
    FinalTextCallback finalTextCb_;
    SimpleCallback acceptedCb_;
    SimpleCallback serverSpeechDetectedCb_;
    ProcessingCallback processingCb_;
    FaceCallback faceCb_;
    ToolCallCallback toolCallCb_;
    VisionCallback visionCb_;

    int16_t* audioTxBuf_;
    uint8_t* audioTxEncodedBuf_;
    size_t audioTxEncodedCapacity_;
    AudioConverter* uploadConverter_;
    AudioConverter* playbackConverter_;
    uint32_t uploadPcmSampleRate_;
    uint8_t uploadPcmChannels_;
    uint8_t* audioRxRawBuf_;
    size_t audioRxRawCapacity_;
    int16_t* audioRxPcmBuf_;
    size_t audioRxPcmCapacity_;
    AudioFrameProvider audioFrameProvider_;
    size_t audioTxFrameSamples_;
    uint32_t audioTxSlowBackoffMs_;
    uint32_t audioTxFailBackoffMs_;
    uint32_t keepaliveIntervalMs_;
    uint32_t audioTxResumeMs_;
    uint32_t lastAudioSendMs_;
    char* invokeAudioBuf_;
    size_t invokeAudioBufCapacity_;

    void generateSessionId();
    void sendStart();
    bool pumpAudioUpload();
    void pumpKeepalive();
    bool decodeIncomingAudio(const char* base64Data, size_t base64Len,
                             const AudioFormat& wireFormat, IncomingAudioChunk& chunk);
    bool ensureRxRawCapacity(size_t bytes);
    bool ensureRxPcmCapacity(size_t samples);
    size_t downmixToMono(const int16_t* input, size_t sampleCount, uint8_t channels);
    static void onEventStatic(WStype_t type, uint8_t* payload, size_t length);
    void onEvent(WStype_t type, uint8_t* payload, size_t length);
};

}  // namespace aiavatar
