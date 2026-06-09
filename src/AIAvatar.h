#pragma once

#include "MicrophoneInput.h"
#include "SpeakerOutput.h"
#include "CameraController.h"
#include "Config.h"
#include "ScreenRenderer.h"
#include "FaceController.h"
#include "IdleMotionEstimator.h"
#include "LedController.h"
#include "MotionController.h"
#include "OpenClawEffects.h"
#include "OtaUpdater.h"
#include "ResourceProvider.h"
#include "SleepManager.h"
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
    bool useStackChan(const Config& config);
    void update();
    void setVolume(uint8_t volume);
    void setVolumeLevel(uint8_t levelIndex);
    void setDisplayBrightness(uint8_t brightness);
    void setMicMuted(bool muted);
    void toggleMicMuted();
    void setSpeakerMuted(bool muted);
    void toggleSpeakerMuted();
    void setIdleMotionEnabled(bool enabled);
    void toggleIdleMotionEnabled();
    void setIdleMotionIntervalSeconds(uint8_t seconds);
    void setIdleMotionType(IdleMotionType type);
    void cycleIdleMotionType();
    void setIdleAnimationEnabled(bool enabled) { setIdleMotionEnabled(enabled); }
    void toggleIdleAnimationEnabled() { toggleIdleMotionEnabled(); }
    void setIdleAnimationIntervalSeconds(uint8_t seconds) {
        setIdleMotionIntervalSeconds(seconds);
    }
    void cycleVolume();
    bool cancelPlayback();
    bool startPushToTalk();
    void endPushToTalk();
    void sendStop();
    void connectWebSocket();
    void disconnectWebSocket();
    void switchWiFi(uint8_t networkIndex);
    bool invokeText(const char* text);
    void resetSleepTimer(const char* reason);
    bool checkForFirmwareUpdate();
    bool startFirmwareUpdate();

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
    bool isSpeakerMuted() const { return speakerMuted_; }
    bool idleMotionEnabled() const { return config_.idleMotionEnabled; }
    uint8_t idleMotionIntervalSeconds() const { return config_.idleMotionIntervalSeconds; }
    IdleMotionType idleMotionType() const { return config_.idleMotionType; }
    bool idleAnimationEnabled() const { return idleMotionEnabled(); }
    uint8_t idleAnimationIntervalSeconds() const { return idleMotionIntervalSeconds(); }
    bool isServerProcessing() const { return serverProcessing_; }
    bool isPushToTalkActive() const { return pushToTalkActive_; }
    bool isSleeping() const { return sleepManager_.isSleeping(); }
    bool isWifiOffForSleep() const { return sleepManager_.isWifiOff(); }
    bool wasSleepWakeTriggered() const { return sleepManager_.wakeActivityTriggered(); }
    uint8_t currentDisplayBrightness() const { return sleepManager_.currentDisplayBrightness(); }
    uint8_t displayBrightness() const { return config_.displayBrightness; }
    uint8_t currentVolumeLevel() const { return volumeLevelIndex_; }
    uint8_t volumeLevelCount() const { return config_.volumeLevelCount; }
    uint8_t currentVolume() const { return volume_; }
    const char* firmwareVersion() const;
    const char* firmwareReleaseDate() const;
    const char* otaManifestUrl() const { return config_.otaManifestUrl; }
    OtaUpdateStatus otaUpdateStatus() const { return otaUpdater_.status(); }
    const char* otaStatusMessage() const { return otaUpdater_.statusMessage(); }
    int otaProgressPercent() const { return otaUpdater_.progressPercent(); }
    bool otaUpdateAvailable() const { return otaUpdater_.updateAvailable(); }
    bool otaBusy() const { return otaUpdater_.busy(); }
    const OtaManifest& otaManifest() const { return otaUpdater_.manifest(); }
    bool isStartupComplete() const {
        return !config_.fastStartup ||
               (deferredStartupStage_ >= 7 && face_.deferredLoadingComplete());
    }

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
    OtaUpdater otaUpdater_;
    SleepManager sleepManager_;
    ResourceProvider defaultResources_;

    volatile bool micMuted_;
    volatile bool speakerMuted_;
    volatile bool serverProcessing_;
    volatile bool wsConnectPending_;
    volatile bool wsDisconnectPending_;
    volatile bool playbackActive_;
    volatile bool pushToTalkActive_;
    volatile bool pttSendPending_;
    volatile bool visionRequestPending_;
    volatile bool wsStopPending_;
    bool stackChanHardwareEnabled_;
    bool wifiStarted_;
    bool speakerReady_;
    bool websocketReady_;
    bool openClawReady_;
    bool deferredImagesLogged_;
    uint8_t deferredStartupStage_;
    uint32_t deferredStartupNextMs_;
    uint32_t heavyDeferredResumeMs_;
    uint8_t volume_;
    uint8_t volumeLevelIndex_;
    uint32_t volumeOverlayUntilMs_;
    bool brightnessSettingDirty_;
    bool volumeSettingDirty_;
    bool micSettingDirty_;
    bool wifiSettingDirty_;
    bool idleMotionSettingDirty_;
    uint32_t settingsSaveDueMs_;
    int8_t batteryLevel_;
    bool batteryCharging_;
    uint32_t lastBatteryCheckMs_;
    volatile bool wifiSwitching_;
    volatile bool wifiConnectStarted_;
    bool wifiConnectedLogged_;
    bool timeConfigured_;
    uint8_t pendingWifiIndex_;
    uint8_t activeWifiNetworkIndex_;
    uint32_t wifiSwitchStartMs_;
    int16_t* pttBuf_;
    size_t pttBufCapacity_;
    volatile size_t pttBufPos_;
    uint32_t pttStartMs_;
    uint32_t pttSendRetryMs_;
    uint8_t* visionPreviewJpg_;
    size_t visionPreviewJpgLen_;
    uint32_t visionPreviewUntilMs_;
    SemaphoreHandle_t visionPreviewMutex_;
    IdleAudioAccumulator idleAudioAccumulator_;
    uint32_t nextIdleMotionMs_;

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
    bool beginNormal();
    bool beginFast();
    static void micTaskFunc(void* params);
    static void speakerTaskFunc(void* params);
    static void wsTaskFunc(void* params);
    void runMicCapture();
    void runSpeakerPlayback();
    void runWebSocket();
    void handlePttSend();
    void handleInvokeTextSend();
    void handleVisionRequest();
    void updateDeferredStartup();
    void beginDeferredWiFi();
    void beginDeferredSpeaker();
    void beginDeferredWebSocket();
    void beginDeferredOpenClaw();
    bool canRunHeavyDeferredWork() const;
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
    void loadPersistedSettings();
    void queueSettingsSave(bool brightness, bool volume, bool mic, bool wifi, bool idleMotion);
    void updatePersistedSettings();
    uint8_t findConfiguredWifiNetworkIndex() const;
    uint8_t nearestVolumeLevel(uint8_t volume) const;
    uint8_t effectiveSpeakerVolume() const;
    bool hasSpeech(const int16_t* samples, size_t sampleCount) const;
    static bool readMicFrameStatic(int16_t* dest, void* context);
    static void clearMicFramesStatic(void* context);

    static void onAudioChunkStatic(const IncomingAudioChunk& chunk);
    static void onFinalStatic();
    static void onFinalTextStatic(const char* responseText, const char* voiceText);
    static void onStopStatic();
    static void onProcessingStatic(bool processing);
    static void onErrorStatic();
    static void onStartStatic(const char* text);
    static void onToolCallStatic(const char* toolName);
    static void onVisionStatic();
    static void onAcceptedStatic();
    static void onNadeStatic();
    static void drawOverlayStatic(LGFX_Sprite* canvas);
};

}  // namespace aiavatar
