#pragma once

#include "MicrophoneInput.h"
#include "SpeakerOutput.h"
#include "CameraController.h"
#include "Config.h"
#include "ScreenRenderer.h"
#include "FaceController.h"
#include "LedController.h"
#include "MotionController.h"
#include "OpenClawEffects.h"
#include "ResourceProvider.h"
#include "StackChanHardware.h"
#include "StatusOverlay.h"
#include "SystemUIController.h"
#include "VisualEffects.h"
#include "WebSocketClient.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace aiavatar {

using SpeechDetectedCallback = void (*)();

class AIAvatar {
public:
    AIAvatar();

    bool begin(const Config& config);
    bool begin(const Config& config, const ResourceProvider& resources);
    bool useStackChan();
    void update();
    void setVolume(uint8_t volume);
    void setVolumeLevel(uint8_t levelIndex);
    void setMicMuted(bool muted);
    void toggleMicMuted();
    void cycleVolume();
    bool startPushToTalk();
    void endPushToTalk();
    void sendStop();
    void connectWebSocket();
    void disconnectWebSocket();
    void switchWiFi(uint8_t networkIndex);
    bool invokeText(const char* text);

    WebSocketClient& websocket() { return ws_; }
    MicrophoneInput& microphone() { return mic_; }
    SpeakerOutput& speaker() { return speaker_; }
    ScreenRenderer& display() { return display_; }
    FaceController& face() { return face_; }
    LedController& leds() { return leds_; }
    MotionController& motion() { return motion_; }
    StatusOverlay& statusOverlay() { return statusOverlay_; }
    SystemUIController& systemUI() { return systemUI_; }
    VisualEffects& visualEffects() { return visualEffects_; }
    void setStackChanAutoAngleSyncEnabled(bool enabled);
    void setUploadAudioConverter(AudioConverter* converter) { ws_.setUploadAudioConverter(converter); }
    void setPlaybackAudioConverter(AudioConverter* converter) {
        ws_.setPlaybackAudioConverter(converter);
    }
    void setOpenClawEffectEnabled(bool enabled);

    bool isConnected() const { return ws_.isConnected(); }
    bool isMicMuted() const { return micMuted_; }
    bool isServerProcessing() const { return serverProcessing_; }
    bool isPushToTalkActive() const { return pushToTalkActive_; }

    void onSpeechDetected(SpeechDetectedCallback cb) { speechDetectedCb_ = cb; }
    void onNade(NadeCallback cb) { userNadeCb_ = cb; }
    void onStart(TextCallback cb) { userStartCb_ = cb; }
    void onFinal(FinalTextCallback cb) { userFinalCb_ = cb; }
    void onToolCall(ToolCallCallback cb) { userToolCallCb_ = cb; }
    void onAccepted(SimpleCallback cb) { userAcceptedCb_ = cb; }
    void onOverlay(ScreenOverlayCallback cb) { userOverlayCb_ = cb; display_.setDirty(); }
    void setStatusOverlayEnabled(bool enabled);

private:
    static constexpr size_t kInvokeTextMaxLen = 768;
    struct InvokeTextMessage {
        char text[kInvokeTextMaxLen];
    };

    Config config_;
    WebSocketClient ws_;
    MicrophoneInput mic_;
    SpeakerOutput speaker_;
    ScreenRenderer display_;
    FaceController face_;
    LedController leds_;
    MotionController motion_;
    StackChanHardware stackChanHardware_;
    CameraController camera_;
    StatusOverlay statusOverlay_;
    SystemUIController systemUI_;
    VisualEffects visualEffects_;
    OpenClawEffects openClaw_;
    ResourceProvider defaultResources_;

    volatile bool micMuted_;
    volatile bool serverProcessing_;
    volatile bool wsConnectPending_;
    volatile bool wsDisconnectPending_;
    volatile bool playbackActive_;
    volatile bool pushToTalkActive_;
    volatile bool pttSendPending_;
    volatile bool visionRequestPending_;
    volatile bool wsStopPending_;
    bool stackChanHardwareEnabled_;
    uint8_t volume_;
    uint8_t volumeLevelIndex_;
    uint32_t volumeOverlayUntilMs_;
    int8_t batteryLevel_;
    bool batteryCharging_;
    uint32_t lastBatteryCheckMs_;
    volatile bool wifiSwitching_;
    volatile bool wifiConnectStarted_;
    bool wifiConnectedLogged_;
    bool timeConfigured_;
    uint8_t pendingWifiIndex_;
    uint32_t wifiSwitchStartMs_;
    int16_t* pttBuf_;
    size_t pttBufCapacity_;
    volatile size_t pttBufPos_;
    uint32_t pttStartMs_;
    uint8_t* visionPreviewJpg_;
    size_t visionPreviewJpgLen_;
    uint32_t visionPreviewUntilMs_;
    SemaphoreHandle_t visionPreviewMutex_;

    TaskHandle_t micTaskHandle_;
    TaskHandle_t speakerTaskHandle_;
    TaskHandle_t wsTaskHandle_;
    QueueHandle_t invokeTextQueue_;

    SpeechDetectedCallback speechDetectedCb_;
    TextCallback userStartCb_;
    FinalTextCallback userFinalCb_;
    ToolCallCallback userToolCallCb_;
    SimpleCallback userAcceptedCb_;
    NadeCallback userNadeCb_;
    ScreenOverlayCallback userOverlayCb_;

    static AIAvatar* s_instance;
    static void micTaskFunc(void* params);
    static void speakerTaskFunc(void* params);
    static void wsTaskFunc(void* params);
    void runMicCapture();
    void runSpeakerPlayback();
    void runWebSocket();
    void handlePttSend();
    void handleInvokeTextSend();
    void handleVisionRequest();
    bool queueInvokeText(const char* text);
    void beginWiFi();
    void updateWiFi();
    void interruptPlaybackForNewResponse();
    void updateWifiSwitch();
    void updateStatusOverlay();
    void showVisionPreview(const uint8_t* jpgBuf, size_t jpgLen);
    void updateVisionPreview();
    void drawVisionPreview(LGFX_Sprite* canvas);
    void logMemoryUsage(const char* label) const;
    uint8_t nearestVolumeLevel(uint8_t volume) const;
    bool hasSpeech(const int16_t* samples, size_t sampleCount) const;
    static bool readMicFrameStatic(int16_t* dest, void* context);
    static void clearMicFramesStatic(void* context);

    static void onAudioChunkStatic(const IncomingAudioChunk& chunk);
    static void onFinalStatic();
    static void onFinalTextStatic(const char* responseText, const char* voiceText);
    static void onStopStatic();
    static void onProcessingStatic(bool processing);
    static void onStartStatic(const char* text);
    static void onToolCallStatic(const char* toolName);
    static void onVisionStatic();
    static void onAcceptedStatic();
    static void onNadeStatic();
    static void drawOverlayStatic(LGFX_Sprite* canvas);
};

}  // namespace aiavatar
