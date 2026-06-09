#include "AIAvatar.h"

#include "FirmwareInfo.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include "mbedtls/base64.h"

namespace aiavatar {

namespace {

constexpr const char* kSettingsPrefsNamespace = "aiavatar";
constexpr const char* kPrefsDisplayBrightnessKey = "disp_brt";
constexpr const char* kPrefsSpeakerVolumeKey = "spk_vol";
constexpr const char* kPrefsMicMutedKey = "mic_mute";
constexpr const char* kPrefsSpeakerMutedKey = "spk_mute";
constexpr const char* kPrefsWifiIndexKey = "wifi_idx";
constexpr const char* kPrefsIdleMotionEnabledKey = "idle_en";
constexpr const char* kPrefsIdleMotionIntervalKey = "idle_int";
constexpr const char* kPrefsIdleMotionTypeKey = "idle_type";
constexpr uint8_t kNoWifiNetworkIndex = 0xff;
constexpr uint32_t kSettingsSaveDebounceMs = 700;

}  // namespace

AIAvatar* AIAvatar::s_instance = nullptr;

AIAvatar::AIAvatar()
    : micMuted_(false),
      speakerMuted_(false),
      serverProcessing_(false),
      wsConnectPending_(false),
      wsDisconnectPending_(false),
      playbackActive_(false),
      pushToTalkActive_(false),
      pttSendPending_(false),
      visionRequestPending_(false),
      wsStopPending_(false),
      stackChanHardwareEnabled_(false),
      wifiStarted_(false),
      speakerReady_(false),
      websocketReady_(false),
      openClawReady_(false),
      deferredImagesLogged_(false),
      deferredStartupStage_(0),
      deferredStartupNextMs_(0),
      heavyDeferredResumeMs_(0),
      volume_(200),
      volumeLevelIndex_(0),
      volumeOverlayUntilMs_(0),
      brightnessSettingDirty_(false),
      volumeSettingDirty_(false),
      micSettingDirty_(false),
      wifiSettingDirty_(false),
      idleMotionSettingDirty_(false),
      settingsSaveDueMs_(0),
      batteryLevel_(-1),
      batteryCharging_(false),
      lastBatteryCheckMs_(0),
      wifiSwitching_(false),
      wifiConnectStarted_(false),
      wifiConnectedLogged_(false),
      timeConfigured_(false),
      pendingWifiIndex_(0),
      activeWifiNetworkIndex_(kNoWifiNetworkIndex),
      wifiSwitchStartMs_(0),
      pttBuf_(nullptr),
      pttBufCapacity_(0),
      pttBufPos_(0),
      pttStartMs_(0),
      pttSendRetryMs_(0),
      visionPreviewJpg_(nullptr),
      visionPreviewJpgLen_(0),
      visionPreviewUntilMs_(0),
      visionPreviewMutex_(nullptr),
      idleAudioAccumulator_{},
      nextIdleMotionMs_(0),
      micTaskHandle_(nullptr),
      speakerTaskHandle_(nullptr),
      wsTaskHandle_(nullptr),
      invokeTextQueue_(nullptr),
      speechDetectedCb_(nullptr),
      userStartCb_(nullptr),
      userFinalCb_(nullptr),
      userToolCallCb_(nullptr),
      userAcceptedCb_(nullptr),
      userNadeCb_(nullptr),
      userOverlayCb_(nullptr) {}

bool AIAvatar::useStackChan() {
    if (!stackChanHardware_.begin()) return false;
    stackChanHardwareEnabled_ = true;
    motion_.setHardware(&stackChanHardware_);
    leds_.setHardware(&stackChanHardware_);
    return true;
}

bool AIAvatar::useStackChan(const Config& config) {
    if (!stackChanHardware_.begin(config)) return false;
    stackChanHardwareEnabled_ = true;
    motion_.setHardware(&stackChanHardware_);
    leds_.setHardware(&stackChanHardware_);
    return true;
}

bool AIAvatar::begin(const Config& config) {
    ResourceProvider resources;
    resources.useSD(true);
    return begin(config, resources);
}

bool AIAvatar::begin(const Config& config, const ResourceProvider& resources) {
    s_instance = this;
    defaultResources_ = resources;
    config_ = config;
    loadPersistedSettings();
    idleAudioAccumulator_.reset();
    nextIdleMotionMs_ = 0;
    otaUpdater_.begin(config_);
    volumeLevelIndex_ = nearestVolumeLevel(config_.speakerVolume);
    volume_ = config_.speakerVolume;
    speaker_.setAutoNormalize(config_.audioNormalizeTargetPeak,
                              config_.audioNormalizeMaxGain);
    setenv("TZ", config_.timezone, 1);
    tzset();
    if (!visionPreviewMutex_) {
        visionPreviewMutex_ = xSemaphoreCreateMutex();
    }

    if (config_.fastStartup) {
        bool ok = beginFast();
        if (ok) sleepManager_.begin(*this, config_);
        return ok;
    }
    bool ok = beginNormal();
    if (ok) sleepManager_.begin(*this, config_);
    return ok;
}

bool AIAvatar::beginFast() {
    display_.setResourceProvider(&defaultResources_);
    if (!display_.begin(config_.displayRotation, config_.displayBrightness)) {
        Serial.println("[AIAvatar] display init failed");
    } else if (!face_.beginMinimal(display_)) {
        Serial.println("[AIAvatar] face init failed");
    }
    face_.setDeferredLoadingEnabled(false);
    statusOverlay_.setEnabled(config_.statusOverlayEnabled);
    systemUI_.begin(*this, config_, statusOverlay_);
    leds_.begin(config_);
    stackChanHardware_.setAutoAngleSyncEnabled(config_.stackChanAutoAngleSync);
    motion_.begin(config_.pitchHome);
    motion_.onNade(AIAvatar::onNadeStatic);
    if (stackChanHardwareEnabled_) {
        camera_.begin();
    }
    display_.onOverlay(AIAvatar::drawOverlayStatic);
    updateStatusOverlay();
    display_.update();

    mic_.configure(config_.micSampleRate, config_.micMagnification, config_.micBufferSamples);
    if (!mic_.beginQueue(2)) {
        Serial.println("[AIAvatar] mic queue init failed");
        return false;
    }
    invokeTextQueue_ = xQueueCreate(2, sizeof(InvokeTextMessage));
    if (!invokeTextQueue_) {
        Serial.println("[AIAvatar] invoke text queue init failed");
        return false;
    }
    pttBufCapacity_ = static_cast<size_t>(config_.micSampleRate) * config_.pttMaxSeconds;
    if (pttBufCapacity_ > 0) {
        pttBuf_ = static_cast<int16_t*>(ps_malloc(pttBufCapacity_ * sizeof(int16_t)));
        if (!pttBuf_) pttBuf_ = static_cast<int16_t*>(malloc(pttBufCapacity_ * sizeof(int16_t)));
    }
    if (!pttBuf_) {
        Serial.println("[AIAvatar] PTT buffer allocation failed");
        return false;
    }
    Serial.printf("[AIAvatar] PTT buffer=%u samples %uKB\n",
                  pttBufCapacity_, (pttBufCapacity_ * sizeof(int16_t)) / 1024);
    mic_.begin();
    ws_.setUploadPcmFormat(config_.micSampleRate, 1);
    if (!ws_.reserveInvokeAudioBuffer(pttBufCapacity_)) {
        Serial.println("[AIAvatar] PTT invoke audio buffer allocation failed");
        return false;
    }

    AudioFrameProvider micProvider = {
        AIAvatar::readMicFrameStatic,
        AIAvatar::clearMicFramesStatic,
        this,
    };
    if (!ws_.configureAudioUpload(micProvider, config_.micBufferSamples, config_.micTxSlowBackoffMs,
                                  config_.micTxFailBackoffMs, config_.keepaliveIntervalMs)) {
        Serial.println("[AIAvatar] audio upload init failed");
        return false;
    }

    xTaskCreatePinnedToCore(AIAvatar::micTaskFunc, "AIAvatarMic",
                            config_.audioTaskStackSize, this, 1, &micTaskHandle_,
                            config_.audioTaskCore);
    deferredStartupStage_ = 0;
    deferredStartupNextMs_ = millis() + 50;
    logMemoryUsage("after fast begin");
    return true;
}

bool AIAvatar::beginNormal() {
    beginWiFi();
    wifiStarted_ = true;
    if (!speaker_.begin(config_.playbackQueueDepth, config_.playbackStartThreshold)) {
        return false;
    }
    speaker_.setVolume(effectiveSpeakerVolume());
    speakerReady_ = true;

    display_.setResourceProvider(&defaultResources_);
    if (!display_.begin(config_.displayRotation, config_.displayBrightness)) {
        Serial.println("[AIAvatar] display init failed");
    } else if (!face_.begin(display_)) {
        Serial.println("[AIAvatar] face init failed");
    }
    face_.setDeferredLoadingEnabled(false);
    statusOverlay_.setEnabled(config_.statusOverlayEnabled);
    systemUI_.begin(*this, config_, statusOverlay_);
    leds_.begin(config_);
    stackChanHardware_.setAutoAngleSyncEnabled(config_.stackChanAutoAngleSync);
    motion_.begin(config_.pitchHome);
    motion_.onNade(AIAvatar::onNadeStatic);
    if (stackChanHardwareEnabled_) {
        camera_.begin();
    }
    openClaw_.begin(display_, leds_);
    openClaw_.preload();
    openClawReady_ = true;
    display_.onOverlay(AIAvatar::drawOverlayStatic);

    mic_.configure(config_.micSampleRate, config_.micMagnification, config_.micBufferSamples);
    if (!mic_.beginQueue(2)) {
        Serial.println("[AIAvatar] mic queue init failed");
        return false;
    }
    invokeTextQueue_ = xQueueCreate(2, sizeof(InvokeTextMessage));
    if (!invokeTextQueue_) {
        Serial.println("[AIAvatar] invoke text queue init failed");
        return false;
    }
    pttBufCapacity_ = static_cast<size_t>(config_.micSampleRate) * config_.pttMaxSeconds;
    if (pttBufCapacity_ > 0) {
        pttBuf_ = static_cast<int16_t*>(ps_malloc(pttBufCapacity_ * sizeof(int16_t)));
        if (!pttBuf_) pttBuf_ = static_cast<int16_t*>(malloc(pttBufCapacity_ * sizeof(int16_t)));
    }
    if (!pttBuf_) {
        Serial.println("[AIAvatar] PTT buffer allocation failed");
        return false;
    }
    Serial.printf("[AIAvatar] PTT buffer=%u samples %uKB\n",
                  pttBufCapacity_, (pttBufCapacity_ * sizeof(int16_t)) / 1024);
    mic_.begin();
    ws_.setUploadPcmFormat(config_.micSampleRate, 1);
    if (!ws_.reserveInvokeAudioBuffer(pttBufCapacity_)) {
        Serial.println("[AIAvatar] PTT invoke audio buffer allocation failed");
        return false;
    }

    AudioFrameProvider micProvider = {
        AIAvatar::readMicFrameStatic,
        AIAvatar::clearMicFramesStatic,
        this,
    };
    if (!ws_.configureAudioUpload(micProvider, config_.micBufferSamples, config_.micTxSlowBackoffMs,
                                  config_.micTxFailBackoffMs, config_.keepaliveIntervalMs)) {
        Serial.println("[AIAvatar] audio upload init failed");
        return false;
    }

    ws_.onAudioChunk(AIAvatar::onAudioChunkStatic);
    ws_.onFinal(AIAvatar::onFinalStatic);
    ws_.onFinalText(AIAvatar::onFinalTextStatic);
    ws_.onStop(AIAvatar::onStopStatic);
    ws_.onProcessing(AIAvatar::onProcessingStatic);
    ws_.onStart(AIAvatar::onStartStatic);
    ws_.onToolCall(AIAvatar::onToolCallStatic);
    ws_.onVision(AIAvatar::onVisionStatic);
    ws_.onAccepted(AIAvatar::onAcceptedStatic);
    ws_.onError(AIAvatar::onErrorStatic);
    if (config_.wsHost[0] != '\0') {
        ws_.begin(config_.wsHost, config_.wsPort, config_.wsPath, config_.userId,
                  config_.wsReconnectIntervalMs, config_.channel, config_.apiKey);
    } else {
        Serial.println("[AIAvatar] WS host is empty; websocket disabled");
    }
    websocketReady_ = true;

    xTaskCreatePinnedToCore(AIAvatar::micTaskFunc, "AIAvatarMic",
                            config_.audioTaskStackSize, this, 1, &micTaskHandle_,
                            config_.audioTaskCore);
    xTaskCreatePinnedToCore(AIAvatar::speakerTaskFunc, "AIAvatarSpeaker",
                            config_.audioTaskStackSize, this, 1, &speakerTaskHandle_,
                            config_.audioTaskCore);
    xTaskCreatePinnedToCore(AIAvatar::wsTaskFunc, "AIAvatarWS",
                            config_.wsTaskStackSize, this, 1, &wsTaskHandle_,
                            config_.wsTaskCore);
    deferredStartupStage_ = 7;
    logMemoryUsage("after begin");
    return true;
}

void AIAvatar::update() {
    if (config_.fastStartup) updateDeferredStartup();
    sleepManager_.resetWakeActivity();
    if (!motion_.updateHardware()) {
        M5.update();
    }
    updateWiFi();
    systemUI_.update();
    updatePersistedSettings();
    if (otaUpdater_.consumeChanged()) {
        display_.setDirty();
    }
    if (config_.fastStartup && deferredStartupStage_ >= 7) {
        face_.setDeferredLoadingEnabled(canRunHeavyDeferredWork());
    }
    face_.update(speaker_.isPlaying(), speaker_.lastChunkRms());
    if (config_.fastStartup && deferredStartupStage_ >= 7 && !deferredImagesLogged_ &&
        face_.deferredLoadingComplete()) {
        deferredImagesLogged_ = true;
        logMemoryUsage("after deferred images");
    }
    motion_.update(playbackActive_ && !systemUI_.settingsOpen());
    leds_.update();
    openClaw_.update();
    if (visualEffects_.update()) {
        display_.setDirty();
    }
    updateVisionPreview();
    updateStatusOverlay();
    display_.update();
    sleepManager_.update();
}

void AIAvatar::updateDeferredStartup() {
    uint32_t now = millis();
    if (static_cast<int32_t>(now - deferredStartupNextMs_) < 0) return;

    switch (deferredStartupStage_) {
        case 0:
            beginDeferredWiFi();
            deferredStartupStage_ = 1;
            deferredStartupNextMs_ = now + 120;
            break;
        case 1:
            if (pushToTalkActive_) {
                deferredStartupNextMs_ = now + 200;
                break;
            }
            face_.loadNextDeferredSprite(true);
            deferredStartupStage_ = 2;
            deferredStartupNextMs_ = now + 120;
            break;
        case 2:
            if (config_.wsHost[0] != '\0' && WiFi.status() != WL_CONNECTED) {
                deferredStartupNextMs_ = now + 250;
                break;
            }
            beginDeferredWebSocket();
            deferredStartupStage_ = 3;
            deferredStartupNextMs_ = now + 120;
            break;
        case 3:
            face_.loadNextDeferredSprite(true);
            deferredStartupStage_ = 4;
            deferredStartupNextMs_ = now + 80;
            break;
        case 4:
            face_.loadNextDeferredSprite(true);
            deferredStartupStage_ = 5;
            deferredStartupNextMs_ = now + 80;
            break;
        case 5:
            if (pushToTalkActive_) {
                deferredStartupNextMs_ = now + 200;
                break;
            }
            beginDeferredSpeaker();
            deferredStartupStage_ = 6;
            deferredStartupNextMs_ = now + 120;
            break;
        case 6:
            if (config_.wsHost[0] != '\0' && !ws_.isConnected()) {
                deferredStartupNextMs_ = now + 250;
                break;
            }
            if (!canRunHeavyDeferredWork()) {
                deferredStartupNextMs_ = now + 250;
                break;
            }
            beginDeferredOpenClaw();
            face_.setDeferredLoadingEnabled(true);
            deferredStartupStage_ = 7;
            logMemoryUsage("after OpenClaw preload");
            break;
        default:
            break;
    }
}

bool AIAvatar::canRunHeavyDeferredWork() const {
    uint32_t now = millis();
    if (static_cast<int32_t>(now - heavyDeferredResumeMs_) < 0) return false;
    return !pushToTalkActive_ && !pttSendPending_ && !serverProcessing_ && !playbackActive_;
}

void AIAvatar::beginDeferredWiFi() {
    if (wifiStarted_) return;
    wifiStarted_ = true;
    beginWiFi();
}

void AIAvatar::beginDeferredSpeaker() {
    if (speakerReady_) return;
    if (!speaker_.begin(config_.playbackQueueDepth, config_.playbackStartThreshold)) {
        Serial.println("[AIAvatar] speaker init failed");
        return;
    }
    speaker_.setVolume(effectiveSpeakerVolume());
    speakerReady_ = true;
    xTaskCreatePinnedToCore(AIAvatar::speakerTaskFunc, "AIAvatarSpeaker",
                            config_.audioTaskStackSize, this, 1, &speakerTaskHandle_,
                            config_.audioTaskCore);
}

void AIAvatar::beginDeferredWebSocket() {
    if (websocketReady_) return;
    ws_.onAudioChunk(AIAvatar::onAudioChunkStatic);
    ws_.onFinal(AIAvatar::onFinalStatic);
    ws_.onFinalText(AIAvatar::onFinalTextStatic);
    ws_.onStop(AIAvatar::onStopStatic);
    ws_.onProcessing(AIAvatar::onProcessingStatic);
    ws_.onStart(AIAvatar::onStartStatic);
    ws_.onToolCall(AIAvatar::onToolCallStatic);
    ws_.onVision(AIAvatar::onVisionStatic);
    ws_.onAccepted(AIAvatar::onAcceptedStatic);
    ws_.onError(AIAvatar::onErrorStatic);
    if (config_.wsHost[0] == '\0') {
        Serial.println("[AIAvatar] WS host is empty; websocket disabled");
        websocketReady_ = true;
        return;
    }
    ws_.begin(config_.wsHost, config_.wsPort, config_.wsPath, config_.userId,
              config_.wsReconnectIntervalMs, config_.channel, config_.apiKey);
    websocketReady_ = true;
    xTaskCreatePinnedToCore(AIAvatar::wsTaskFunc, "AIAvatarWS",
                            config_.wsTaskStackSize, this, 1, &wsTaskHandle_,
                            config_.wsTaskCore);
}

void AIAvatar::beginDeferredOpenClaw() {
    if (openClawReady_) return;
    openClaw_.begin(display_, leds_);
    openClaw_.preload();
    openClawReady_ = true;
}


const char* AIAvatar::firmwareVersion() const {
    return kFirmwareVersion;
}

const char* AIAvatar::firmwareReleaseDate() const {
    return kFirmwareReleaseDate;
}

bool AIAvatar::checkForFirmwareUpdate() {
    resetSleepTimer("OTA check");
    bool ok = otaUpdater_.checkForUpdate();
    display_.setDirty();
    return ok;
}

bool AIAvatar::startFirmwareUpdate() {
    resetSleepTimer("OTA update");
    if (otaUpdater_.updateAvailable()) {
        wsDisconnectPending_ = true;
    }
    bool ok = otaUpdater_.startUpdate();
    display_.setDirty();
    return ok;
}

void AIAvatar::setVolume(uint8_t volume) {
    resetSleepTimer("volume");
    volumeLevelIndex_ = nearestVolumeLevel(volume);
    volume_ = volume;
    config_.speakerVolume = volume;
    volumeOverlayUntilMs_ = millis() + 2000;
    speaker_.setVolume(effectiveSpeakerVolume());
    display_.setDirty();
    queueSettingsSave(false, true, false, false, false);
}

void AIAvatar::setVolumeLevel(uint8_t levelIndex) {
    if (config_.volumeLevelCount == 0) return;
    resetSleepTimer("volume");
    if (levelIndex >= config_.volumeLevelCount) levelIndex = config_.volumeLevelCount - 1;
    volumeLevelIndex_ = levelIndex;
    volume_ = config_.volumeLevels[volumeLevelIndex_];
    config_.speakerVolume = volume_;
    volumeOverlayUntilMs_ = millis() + 2000;
    speaker_.setVolume(effectiveSpeakerVolume());
    display_.setDirty();
    queueSettingsSave(false, true, false, false, false);
}

void AIAvatar::setDisplayBrightness(uint8_t brightness) {
    resetSleepTimer("brightness");
    config_.displayBrightness = brightness;
    M5.Display.setBrightness(brightness);
    display_.setDirty();
    queueSettingsSave(true, false, false, false, false);
}

void AIAvatar::setMicMuted(bool muted) {
    resetSleepTimer("mic mute");
    micMuted_ = muted;
    display_.setDirty();
    queueSettingsSave(false, false, true, false, false);
}

void AIAvatar::toggleMicMuted() {
    resetSleepTimer("mic mute");
    micMuted_ = !micMuted_;
    display_.setDirty();
    queueSettingsSave(false, false, true, false, false);
}

void AIAvatar::setSpeakerMuted(bool muted) {
    if (speakerMuted_ == muted) return;
    resetSleepTimer("speaker mute");
    speakerMuted_ = muted;
    if (speakerReady_) speaker_.setVolume(effectiveSpeakerVolume());
    if (speakerMuted_) cancelPlayback();
    display_.setDirty();
    queueSettingsSave(false, true, false, false, false);
}

void AIAvatar::toggleSpeakerMuted() {
    setSpeakerMuted(!speakerMuted_);
}

void AIAvatar::setIdleMotionEnabled(bool enabled) {
    if (config_.idleMotionEnabled == enabled) return;
    resetSleepTimer("idle motion");
    config_.idleMotionEnabled = enabled;
    idleAudioAccumulator_.reset();
    nextIdleMotionMs_ = 0;
    if (!enabled) {
        motion_.goHome();
    }
    display_.setDirty();
    queueSettingsSave(false, false, false, false, true);
}

void AIAvatar::toggleIdleMotionEnabled() {
    setIdleMotionEnabled(!config_.idleMotionEnabled);
}

void AIAvatar::setIdleMotionIntervalSeconds(uint8_t seconds) {
    uint8_t clamped = clampIdleMotionIntervalSeconds(seconds);
    if (config_.idleMotionIntervalSeconds == clamped) return;
    resetSleepTimer("idle motion interval");
    config_.idleMotionIntervalSeconds = clamped;
    nextIdleMotionMs_ = 0;
    display_.setDirty();
    queueSettingsSave(false, false, false, false, true);
}

void AIAvatar::setIdleMotionType(IdleMotionType type) {
    if (config_.idleMotionType == type) return;
    resetSleepTimer("idle motion type");
    config_.idleMotionType = type;
    idleAudioAccumulator_.reset();
    nextIdleMotionMs_ = 0;
    motion_.goHome();
    display_.setDirty();
    queueSettingsSave(false, false, false, false, true);
}

void AIAvatar::cycleIdleMotionType() {
    setIdleMotionType(config_.idleMotionType == IdleMotionType::StereoBalance
                          ? IdleMotionType::Random
                          : IdleMotionType::StereoBalance);
}

void AIAvatar::cycleVolume() {
    if (config_.volumeLevelCount == 0) return;
    setVolumeLevel((volumeLevelIndex_ + 1) % config_.volumeLevelCount);
}

bool AIAvatar::cancelPlayback() {
    bool active = serverProcessing_;
    if (speakerReady_) {
        active = active || playbackActive_ || speaker_.isPlaying() || speaker_.queuedSamples() > 0;
    }
    if (!active) return false;

    resetSleepTimer("playback cancel");
    if (speakerReady_) speaker_.requestImmediateStop();
    serverProcessing_ = false;
    visualEffects_.setProcessing(false);
    visualEffects_.clearToolPulse();
    wsStopPending_ = true;
    if (config_.fastStartup) heavyDeferredResumeMs_ = millis() + 500;
    display_.setDirty();
    Serial.println("[AIAvatar] playback cancel");
    return true;
}

bool AIAvatar::startPushToTalk() {
    if (!micMuted_ || !pttBuf_) return false;
    if (config_.fastStartup && pttSendPending_) {
        Serial.println("[AIAvatar] PTT start blocked: send pending");
        return false;
    }
    serverProcessing_ = false;
    visualEffects_.setProcessing(false);
    if (speakerReady_) speaker_.requestImmediateStop();
    mic_.clearQueue();
    pttBufPos_ = 0;
    pttSendRetryMs_ = 0;
    pttStartMs_ = millis();
    pushToTalkActive_ = true;
    resetSleepTimer("PTT start");
    display_.setDirty();
    Serial.println("[AIAvatar] PTT start");
    return true;
}

void AIAvatar::endPushToTalk() {
    if (!pushToTalkActive_) return;
    resetSleepTimer("PTT end");
    pushToTalkActive_ = false;
    visualEffects_.clearVoiceDetected();

    size_t samples = pttBufPos_;
    size_t minSamples = static_cast<size_t>(config_.pttMinSeconds * config_.micSampleRate);
    if (samples >= minSamples) {
        pttSendPending_ = true;
        Serial.printf("[AIAvatar] PTT end send pending samples=%u\n", samples);
    } else if (samples > 0) {
        Serial.printf("[AIAvatar] PTT discarded samples=%u min=%u\n", samples, minSamples);
    } else {
        Serial.println("[AIAvatar] PTT end no data");
    }
    display_.setDirty();
}

void AIAvatar::setStatusOverlayEnabled(bool enabled) {
    statusOverlay_.setEnabled(enabled);
    display_.setDirty();
}

void AIAvatar::setStackChanAutoAngleSyncEnabled(bool enabled) {
    stackChanHardware_.setAutoAngleSyncEnabled(enabled);
}

void AIAvatar::setOpenClawEffectEnabled(bool enabled) {
    openClaw_.setEnabled(enabled);
}

void AIAvatar::sendStop() {
    resetSleepTimer("stop");
    wsStopPending_ = true;
}

void AIAvatar::connectWebSocket() {
    if (config_.wsHost[0] == '\0') return;
    wsConnectPending_ = true;
}

void AIAvatar::disconnectWebSocket() {
    wsDisconnectPending_ = true;
}

void AIAvatar::switchWiFi(uint8_t networkIndex) {
    if (networkIndex >= config_.wifiNetworkCount) return;
    const auto& network = config_.wifiNetworks[networkIndex];
    if (!network.ssid[0]) return;
    resetSleepTimer("WiFi switch");

    pendingWifiIndex_ = networkIndex;
    wifiSwitching_ = true;
    wifiConnectStarted_ = true;
    wifiConnectedLogged_ = false;
    timeConfigured_ = false;
    wifiSwitchStartMs_ = millis();
    wsConnectPending_ = false;
    wsDisconnectPending_ = true;
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(network.ssid, network.pass);
    Serial.printf("[AIAvatar] switching WiFi to %s\n", network.ssid);
    display_.setDirty();
}

void AIAvatar::micTaskFunc(void* params) {
    static_cast<AIAvatar*>(params)->runMicCapture();
}

void AIAvatar::speakerTaskFunc(void* params) {
    static_cast<AIAvatar*>(params)->runSpeakerPlayback();
}

void AIAvatar::wsTaskFunc(void* params) {
    static_cast<AIAvatar*>(params)->runWebSocket();
}

void AIAvatar::runMicCapture() {
    static int16_t stereoBuf[kMicBufferSamplesMax * 2];
    static int16_t micBuf[kMicBufferSamplesMax];
    uint32_t lastSpeechDetectedMs = 0;

    for (;;) {
        if (playbackActive_) {
            idleAudioAccumulator_.reset();
            nextIdleMotionMs_ = 0;
            delay(1);
            continue;
        }

        if (mic_.readStereo(stereoBuf, config_.micBufferSamples)) {
            MicrophoneInput::downmixStereoToMono(stereoBuf, micBuf, config_.micBufferSamples);

            uint32_t now = millis();
            bool idleMotionAllowed = config_.idleMotionEnabled && !serverProcessing_ &&
                                     !pushToTalkActive_ && !pttSendPending_ &&
                                     !systemUI_.settingsOpen() &&
                                     !motion_.isNadeActive();
            if (idleMotionAllowed) {
                if (idleMotionCooldownReady(now, nextIdleMotionMs_)) {
                    if (config_.idleMotionType == IdleMotionType::Random) {
                        IdleMotionTarget target =
                            randomIdleMotionTarget(config_.pitchHome, esp_random());
                        if (target.active && motion_.moveIdleTarget(target.yaw, target.pitch, 450)) {
                            scheduleNextIdleMotion(now, config_.idleMotionIntervalSeconds,
                                                   true, esp_random(), nextIdleMotionMs_);
                        } else {
                            nextIdleMotionMs_ = 0;
                        }
                    } else {
                        idleAudioAccumulator_.reset();
                        if (addIdleStereoFrame(idleAudioAccumulator_, stereoBuf,
                                               config_.micBufferSamples, config_.vadThresholdDb)) {
                            IdleMotionTarget target =
                                estimateIdleMotionTarget(idleAudioAccumulator_, config_.pitchHome);
                            if (target.active && motion_.moveIdleTarget(target.yaw, target.pitch, 450)) {
                                scheduleNextIdleMotion(now, config_.idleMotionIntervalSeconds,
                                                       false, 0, nextIdleMotionMs_);
                            }
                            idleAudioAccumulator_.reset();
                        }
                    }
                } else {
                    idleAudioAccumulator_.reset();
                }
            } else {
                idleAudioAccumulator_.reset();
                nextIdleMotionMs_ = 0;
            }

            if (pushToTalkActive_) {
                if (hasSpeech(micBuf, config_.micBufferSamples)) {
                    if (visualEffects_.showVoiceDetected(350)) {
                        display_.setDirty();
                    }
                }
                size_t pos = pttBufPos_;
                if (pos + config_.micBufferSamples <= pttBufCapacity_) {
                    memcpy(pttBuf_ + pos, micBuf, config_.micBufferSamples * sizeof(int16_t));
                    pttBufPos_ = pos + config_.micBufferSamples;
                }
                bool timeout = millis() - pttStartMs_ >= static_cast<uint32_t>(config_.pttMaxSeconds) * 1000;
                bool full = pttBufPos_ + config_.micBufferSamples > pttBufCapacity_;
                if (timeout || full) {
                    pushToTalkActive_ = false;
                    pttSendPending_ = true;
                    Serial.printf("[AIAvatar] PTT auto-end %s samples=%u\n",
                                  timeout ? "timeout" : "full", pttBufPos_);
                    display_.setDirty();
                }
                delay(1);
                continue;
            }

            if (!micMuted_ && ws_.isConnected() && !serverProcessing_) {
                if (hasSpeech(micBuf, config_.micBufferSamples)) {
                    if (visualEffects_.showVoiceDetected(350)) {
                        display_.setDirty();
                    }
                    if (speechDetectedCb_ && now - lastSpeechDetectedMs >= 300) {
                        lastSpeechDetectedMs = now;
                        speechDetectedCb_();
                    }
                }
                mic_.enqueueFrame(micBuf);
            }
        }

        delay(1);
    }
}

void AIAvatar::runSpeakerPlayback() {
    static int16_t playBuf[kPlaybackChunkSamples];
    bool speakerMode = false;
    bool playbackEndSeen = false;
    uint32_t waitStartMs = 0;

    for (;;) {
        if (speaker_.consumeImmediateStopRequested()) {
            speaker_.stopHardware();
            speaker_.resetState();
            mic_.begin();
            playbackActive_ = false;
            speakerMode = false;
            playbackEndSeen = false;
            waitStartMs = 0;
            Serial.println("[AIAvatar] audio -> mic (interrupted)");
        }

        if (!speakerMode && speaker_.hasStartThreshold()) {
            playbackActive_ = true;
            mic_.end();
            speaker_.startHardware();
            speakerMode = true;
            playbackEndSeen = false;
            waitStartMs = 0;
            Serial.println("[AIAvatar] audio -> speaker");
        }

        if (speakerMode) {
            if (!M5.Speaker.isPlaying()) {
                PlaybackEvent event;
                if (speaker_.dequeueEvent(event)) {
                    waitStartMs = 0;
                    switch (event.type) {
                        case PlaybackEventType::Format:
                            speaker_.applyFormat(event.sampleRate, event.channels,
                                                 event.bitsPerSample);
                            break;
                        case PlaybackEventType::PcmFrame:
                            memcpy(playBuf, event.samples, event.sampleCount * sizeof(int16_t));
                            speaker_.releaseFrame(event);
                            speaker_.playFrame(playBuf, event.sampleCount);
                            break;
                        case PlaybackEventType::Face:
                            face_.setExpression(static_cast<Expression>(event.faceId),
                                                event.faceDurationMs);
                            break;
                        case PlaybackEventType::End:
                            playbackEndSeen = true;
                            break;
                        case PlaybackEventType::Stop:
                            playbackEndSeen = true;
                            speaker_.clearQueue();
                            break;
                    }
                } else if (playbackEndSeen) {
                    speaker_.stopHardware();
                    speaker_.resetState();
                    mic_.begin();
                    playbackActive_ = false;
                    speakerMode = false;
                    waitStartMs = 0;
                    Serial.println("[AIAvatar] audio -> mic (ended)");
                } else {
                    if (waitStartMs == 0) waitStartMs = millis();
                    if (millis() - waitStartMs >= config_.playbackDrainTimeoutMs) {
                        speaker_.stopHardware();
                        speaker_.resetState();
                        mic_.begin();
                        playbackActive_ = false;
                        speakerMode = false;
                        waitStartMs = 0;
                        Serial.println("[AIAvatar] audio -> mic (timeout)");
                    }
                }
            }
        }

        delay(1);
    }
}

void AIAvatar::runWebSocket() {
    for (;;) {
        if (config_.wsHost[0] == '\0') {
            delay(100);
            continue;
        }
        if (wsDisconnectPending_) {
            wsDisconnectPending_ = false;
            ws_.disconnect();
        }
        if (wsConnectPending_) {
            wsConnectPending_ = false;
            ws_.reconnect(config_.wsHost, config_.wsPort, config_.wsPath, config_.userId,
                          config_.wsReconnectIntervalMs, config_.channel, config_.apiKey);
        }
        if (wsStopPending_) {
            wsStopPending_ = false;
            ws_.sendStop();
        }

        ws_.loop();
        handleInvokeTextSend();
        handlePttSend();
        handleVisionRequest();
        delay(1);
    }
}

void AIAvatar::handleInvokeTextSend() {
    if (!invokeTextQueue_ || !ws_.isConnected()) return;
    InvokeTextMessage msg = {};
    while (xQueueReceive(invokeTextQueue_, &msg, 0) == pdTRUE) {
        bool ok = ws_.sendInvoke(msg.text);
        Serial.printf("[AIAvatar] invoke text %s\n", ok ? "ok" : "failed");
    }
}

void AIAvatar::handlePttSend() {
    if (!pttSendPending_ || !ws_.isConnected()) return;
    if (config_.fastStartup && !speakerReady_) return;
    uint32_t now = millis();
    if (config_.fastStartup && pttSendRetryMs_ != 0 &&
        static_cast<int32_t>(now - pttSendRetryMs_) < 0) {
        return;
    }
    if (!config_.fastStartup) pttSendPending_ = false;
    size_t samples = pttBufPos_;
    if (samples == 0) {
        pttSendPending_ = false;
        return;
    }
    Serial.printf("[AIAvatar] PTT sending samples=%u\n", samples);
    bool ok = ws_.sendInvokeWithAudio(pttBuf_, samples);
    Serial.printf("[AIAvatar] PTT send %s\n", ok ? "ok" : "failed");
    if (!config_.fastStartup) return;
    if (ok) {
        pttSendPending_ = false;
        pttSendRetryMs_ = 0;
        pttBufPos_ = 0;
        heavyDeferredResumeMs_ = now + 10000;
    } else {
        pttSendPending_ = false;
        pttSendRetryMs_ = 0;
        pttBufPos_ = 0;
        heavyDeferredResumeMs_ = now + 1000;
        Serial.println("[AIAvatar] PTT send failed; dropped pending audio");
    }
}

void AIAvatar::handleVisionRequest() {
    if (!visionRequestPending_ || !ws_.isConnected()) return;
    visionRequestPending_ = false;

    if (!camera_.isReady()) {
        Serial.println("[Vision] skipped: camera is not ready");
        return;
    }

    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;
    if (!camera_.captureJpeg(&jpgBuf, &jpgLen)) {
        return;
    }
    Serial.printf("[Vision] JPEG %u bytes\n", static_cast<unsigned>(jpgLen));
    showVisionPreview(jpgBuf, jpgLen);

    static const char kPrefix[] = "data:image/jpeg;base64,";
    size_t prefixLen = sizeof(kPrefix) - 1;
    size_t b64Len = ((jpgLen + 2) / 3) * 4;
    size_t dataUrlLen = prefixLen + b64Len + 1;

    char* dataUrl = static_cast<char*>(ps_malloc(dataUrlLen));
    if (!dataUrl) dataUrl = static_cast<char*>(malloc(dataUrlLen));
    if (!dataUrl) {
        Serial.printf("[Vision] data URL allocation failed (%u bytes)\n",
                      static_cast<unsigned>(dataUrlLen));
        free(jpgBuf);
        return;
    }

    memcpy(dataUrl, kPrefix, prefixLen);
    size_t actualB64Len = 0;
    int err = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(dataUrl + prefixLen),
                                    b64Len + 1, &actualB64Len,
                                    reinterpret_cast<const unsigned char*>(jpgBuf), jpgLen);
    free(jpgBuf);
    if (err != 0) {
        Serial.printf("[Vision] base64 encode failed: %d\n", err);
        free(dataUrl);
        return;
    }
    dataUrl[prefixLen + actualB64Len] = '\0';

    bool ok = ws_.sendInvokeWithImage(config_.visionInvokePrompt, dataUrl);
    free(dataUrl);
    Serial.printf("[Vision] invoke %s\n", ok ? "sent" : "failed");
}

bool AIAvatar::invokeText(const char* text) {
    resetSleepTimer("invoke text");
    return queueInvokeText(text);
}

void AIAvatar::resetSleepTimer(const char* reason) {
    sleepManager_.resetSleepTimer(reason);
}

bool AIAvatar::queueInvokeText(const char* text) {
    if (!invokeTextQueue_ || !text) return false;
    InvokeTextMessage msg = {};
    strlcpy(msg.text, text, sizeof(msg.text));
    if (xQueueSend(invokeTextQueue_, &msg, 0) == pdTRUE) return true;

    InvokeTextMessage dropped = {};
    xQueueReceive(invokeTextQueue_, &dropped, 0);
    return xQueueSend(invokeTextQueue_, &msg, 0) == pdTRUE;
}

void AIAvatar::beginWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    if (config_.wifiSsid[0] == '\0') {
        Serial.println("[AIAvatar] WiFi SSID is empty; starting offline");
        return;
    }

    WiFi.begin(config_.wifiSsid, config_.wifiPass);
    wifiConnectStarted_ = true;
    wifiConnectedLogged_ = false;
    timeConfigured_ = false;
    Serial.printf("[AIAvatar] WiFi connecting: %s\n", config_.wifiSsid);
}

void AIAvatar::updateWiFi() {
    updateWifiSwitch();

    if (!wifiConnectStarted_ || WiFi.status() != WL_CONNECTED) return;

    if (!wifiConnectedLogged_) {
        wifiConnectedLogged_ = true;
        Serial.printf("[AIAvatar] WiFi connected ip=%s rssi=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    if (!timeConfigured_) {
        timeConfigured_ = true;
        configTzTime(config_.timezone, "ntp.nict.jp", "pool.ntp.org", "time.google.com");
    }
}

void AIAvatar::interruptPlaybackForNewResponse() {
    if (!speakerReady_) return;
    speaker_.requestImmediateStop();
    uint32_t startedAt = millis();
    while (speaker_.immediateStopRequested() && millis() - startedAt < 200) {
        delay(1);
    }
}

void AIAvatar::updateWifiSwitch() {
    if (!wifiSwitching_) return;

    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        const auto& network = config_.wifiNetworks[pendingWifiIndex_];
        strlcpy(config_.wifiSsid, network.ssid, sizeof(config_.wifiSsid));
        strlcpy(config_.wifiPass, network.pass, sizeof(config_.wifiPass));
        wifiSwitching_ = false;
        activeWifiNetworkIndex_ = pendingWifiIndex_;
        queueSettingsSave(false, false, false, true, false);
        wsConnectPending_ = true;
        wifiConnectedLogged_ = true;
        Serial.printf("[AIAvatar] WiFi connected to %s ip=%s\n",
                      network.ssid, WiFi.localIP().toString().c_str());
        display_.setDirty();
    } else if (millis() - wifiSwitchStartMs_ >= 15000) {
        wifiSwitching_ = false;
        Serial.println("[AIAvatar] WiFi switch timeout");
        display_.setDirty();
    }
}

void AIAvatar::updateStatusOverlay() {
    uint32_t nowMs = millis();
    if (lastBatteryCheckMs_ == 0 || nowMs - lastBatteryCheckMs_ >= 30000) {
        batteryLevel_ = static_cast<int8_t>(M5.Power.getBatteryLevel());
        batteryCharging_ = M5.Power.isCharging();
        lastBatteryCheckMs_ = nowMs;
    }

    time_t now = time(nullptr);
    struct tm ti = {};
    localtime_r(&now, &ti);

    StatusOverlayState state = {};
    state.micMuted = micMuted_ && !pushToTalkActive_;
    state.speakerMuted = speakerMuted_;
    state.volumeVisible = nowMs < volumeOverlayUntilMs_;
    state.volumeLevelCount = config_.volumeLevelCount;
    state.volumeLevel = volumeLevelIndex_;
    state.wifiConnected = WiFi.status() == WL_CONNECTED;
    state.websocketConnected = ws_.isConnected();
    state.batteryLevel = batteryLevel_;
    state.batteryCharging = batteryCharging_;
    state.hour = static_cast<uint8_t>(ti.tm_hour);
    state.minute = static_cast<uint8_t>(ti.tm_min);

    if (statusOverlay_.update(state)) {
        display_.setDirty();
    }
}

void AIAvatar::showVisionPreview(const uint8_t* jpgBuf, size_t jpgLen) {
    if (!jpgBuf || jpgLen == 0 || !display_.ready()) return;

    uint8_t* preview = static_cast<uint8_t*>(ps_malloc(jpgLen));
    if (!preview) preview = static_cast<uint8_t*>(malloc(jpgLen));
    if (!preview) {
        Serial.printf("[Vision] preview allocation failed (%u bytes)\n",
                      static_cast<unsigned>(jpgLen));
        return;
    }
    memcpy(preview, jpgBuf, jpgLen);

    if (visionPreviewMutex_) {
        xSemaphoreTake(visionPreviewMutex_, portMAX_DELAY);
    }
    free(visionPreviewJpg_);
    visionPreviewJpg_ = preview;
    visionPreviewJpgLen_ = jpgLen;
    visionPreviewUntilMs_ = millis() + config_.visionPreviewDurationMs;
    if (visionPreviewMutex_) {
        xSemaphoreGive(visionPreviewMutex_);
    }
    display_.setDirty();
}

void AIAvatar::updateVisionPreview() {
    if (visionPreviewMutex_) {
        xSemaphoreTake(visionPreviewMutex_, portMAX_DELAY);
    }
    bool expired = visionPreviewJpg_ &&
                   static_cast<int32_t>(millis() - visionPreviewUntilMs_) >= 0;
    if (expired) {
        free(visionPreviewJpg_);
        visionPreviewJpg_ = nullptr;
        visionPreviewJpgLen_ = 0;
        visionPreviewUntilMs_ = 0;
    }
    if (visionPreviewMutex_) {
        xSemaphoreGive(visionPreviewMutex_);
    }
    if (expired) display_.setDirty();
}

void AIAvatar::drawVisionPreview(LGFX_Sprite* canvas) {
    if (!canvas) return;

    if (visionPreviewMutex_) {
        xSemaphoreTake(visionPreviewMutex_, portMAX_DELAY);
    }
    bool active = visionPreviewJpg_ && visionPreviewJpgLen_ > 0 &&
                  static_cast<int32_t>(millis() - visionPreviewUntilMs_) < 0;
    if (active) {
        canvas->fillSprite(TFT_BLACK);
        canvas->drawJpg(visionPreviewJpg_, visionPreviewJpgLen_, 0, 0,
                        canvas->width(), canvas->height());
    }
    if (visionPreviewMutex_) {
        xSemaphoreGive(visionPreviewMutex_);
    }
}

void AIAvatar::logMemoryUsage(const char* label) const {
    constexpr uint32_t kInternalHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    size_t internalTotal = heap_caps_get_total_size(kInternalHeapCaps);
    size_t internalFree = heap_caps_get_free_size(kInternalHeapCaps);
    size_t internalMinFree = heap_caps_get_minimum_free_size(kInternalHeapCaps);
    size_t internalLargest = heap_caps_get_largest_free_block(kInternalHeapCaps);

    size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psramMinFree = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    size_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    auto usedPct = [](size_t total, size_t freeBytes) -> float {
        return total > 0 ? 100.0f * static_cast<float>(total - freeBytes) /
                               static_cast<float>(total)
                         : 0.0f;
    };

    Serial.printf("[Memory] %s internal used=%uKB/%uKB %.1f%% free=%uKB minFree=%uKB largest=%uKB\n",
                  label ? label : "",
                  static_cast<unsigned>((internalTotal - internalFree) / 1024),
                  static_cast<unsigned>(internalTotal / 1024),
                  usedPct(internalTotal, internalFree),
                  static_cast<unsigned>(internalFree / 1024),
                  static_cast<unsigned>(internalMinFree / 1024),
                  static_cast<unsigned>(internalLargest / 1024));
    Serial.printf("[Memory] %s psram used=%uKB/%uKB %.1f%% free=%uKB minFree=%uKB largest=%uKB\n",
                  label ? label : "",
                  static_cast<unsigned>((psramTotal - psramFree) / 1024),
                  static_cast<unsigned>(psramTotal / 1024),
                  usedPct(psramTotal, psramFree),
                  static_cast<unsigned>(psramFree / 1024),
                  static_cast<unsigned>(psramMinFree / 1024),
                  static_cast<unsigned>(psramLargest / 1024));
}


void AIAvatar::loadPersistedSettings() {
    activeWifiNetworkIndex_ = findConfiguredWifiNetworkIndex();

    Preferences prefs;
    if (!prefs.begin(kSettingsPrefsNamespace, true)) {
        Serial.println("[Settings] NVS unavailable; using config values");
        return;
    }

    bool restored = false;
    if (prefs.isKey(kPrefsDisplayBrightnessKey)) {
        config_.displayBrightness = prefs.getUChar(kPrefsDisplayBrightnessKey,
                                                   config_.displayBrightness);
        restored = true;
    }
    if (prefs.isKey(kPrefsSpeakerVolumeKey)) {
        config_.speakerVolume = prefs.getUChar(kPrefsSpeakerVolumeKey,
                                               config_.speakerVolume);
        restored = true;
    }
    if (prefs.isKey(kPrefsMicMutedKey)) {
        micMuted_ = prefs.getBool(kPrefsMicMutedKey, micMuted_);
        restored = true;
    }
    if (prefs.isKey(kPrefsSpeakerMutedKey)) {
        speakerMuted_ = prefs.getBool(kPrefsSpeakerMutedKey, speakerMuted_);
        restored = true;
    }
    if (prefs.isKey(kPrefsIdleMotionEnabledKey)) {
        config_.idleMotionEnabled =
            prefs.getBool(kPrefsIdleMotionEnabledKey, config_.idleMotionEnabled);
        restored = true;
    }
    if (prefs.isKey(kPrefsIdleMotionIntervalKey)) {
        config_.idleMotionIntervalSeconds = clampIdleMotionIntervalSeconds(
            prefs.getUChar(kPrefsIdleMotionIntervalKey,
                           config_.idleMotionIntervalSeconds));
        restored = true;
    }
    if (prefs.isKey(kPrefsIdleMotionTypeKey)) {
        uint8_t type = prefs.getUChar(kPrefsIdleMotionTypeKey,
                                      static_cast<uint8_t>(config_.idleMotionType));
        if (type <= static_cast<uint8_t>(IdleMotionType::Random)) {
            config_.idleMotionType = static_cast<IdleMotionType>(type);
        }
        restored = true;
    }
    if (prefs.isKey(kPrefsWifiIndexKey)) {
        uint8_t index = prefs.getUChar(kPrefsWifiIndexKey, kNoWifiNetworkIndex);
        if (index < config_.wifiNetworkCount && config_.wifiNetworks[index].ssid[0]) {
            const auto& network = config_.wifiNetworks[index];
            strlcpy(config_.wifiSsid, network.ssid, sizeof(config_.wifiSsid));
            strlcpy(config_.wifiPass, network.pass, sizeof(config_.wifiPass));
            activeWifiNetworkIndex_ = index;
            restored = true;
        } else if (index != kNoWifiNetworkIndex) {
            Serial.printf("[Settings] ignored invalid WiFi index from NVS: %u\n", index);
        }
    }
    prefs.end();

    if (restored) {
        Serial.printf("[Settings] NVS restored brightness=%u volume=%u mic=%s speaker=%s wifi=%u idleMotion=%s/%u/%us\n",
                      config_.displayBrightness, config_.speakerVolume,
                      micMuted_ ? "muted" : "on", speakerMuted_ ? "muted" : "on",
                      activeWifiNetworkIndex_,
                      config_.idleMotionEnabled ? "on" : "off",
                      static_cast<unsigned>(config_.idleMotionType),
                      config_.idleMotionIntervalSeconds);
    }
}

void AIAvatar::queueSettingsSave(bool brightness, bool volume, bool mic, bool wifi,
                                 bool idleMotion) {
    if (!brightness && !volume && !mic && !wifi && !idleMotion) return;
    brightnessSettingDirty_ = brightnessSettingDirty_ || brightness;
    volumeSettingDirty_ = volumeSettingDirty_ || volume;
    micSettingDirty_ = micSettingDirty_ || mic;
    wifiSettingDirty_ = wifiSettingDirty_ || wifi;
    idleMotionSettingDirty_ = idleMotionSettingDirty_ || idleMotion;
    settingsSaveDueMs_ = millis() + kSettingsSaveDebounceMs;
}

void AIAvatar::updatePersistedSettings() {
    if (!brightnessSettingDirty_ && !volumeSettingDirty_ && !micSettingDirty_ &&
        !wifiSettingDirty_ && !idleMotionSettingDirty_) {
        return;
    }
    if (static_cast<int32_t>(millis() - settingsSaveDueMs_) < 0) return;

    Preferences prefs;
    if (!prefs.begin(kSettingsPrefsNamespace, false)) {
        Serial.println("[Settings] NVS open failed; retry later");
        settingsSaveDueMs_ = millis() + 5000;
        return;
    }

    if (brightnessSettingDirty_) {
        prefs.putUChar(kPrefsDisplayBrightnessKey, config_.displayBrightness);
    }
    if (volumeSettingDirty_) {
        prefs.putUChar(kPrefsSpeakerVolumeKey, volume_);
        prefs.putBool(kPrefsSpeakerMutedKey, speakerMuted_);
    }
    if (micSettingDirty_) {
        prefs.putBool(kPrefsMicMutedKey, micMuted_);
    }
    if (wifiSettingDirty_ && activeWifiNetworkIndex_ != kNoWifiNetworkIndex) {
        prefs.putUChar(kPrefsWifiIndexKey, activeWifiNetworkIndex_);
    }
    if (idleMotionSettingDirty_) {
        prefs.putBool(kPrefsIdleMotionEnabledKey, config_.idleMotionEnabled);
        prefs.putUChar(kPrefsIdleMotionIntervalKey,
                       config_.idleMotionIntervalSeconds);
        prefs.putUChar(kPrefsIdleMotionTypeKey, static_cast<uint8_t>(config_.idleMotionType));
    }
    prefs.end();

    brightnessSettingDirty_ = false;
    volumeSettingDirty_ = false;
    micSettingDirty_ = false;
    wifiSettingDirty_ = false;
    idleMotionSettingDirty_ = false;
    settingsSaveDueMs_ = 0;
    Serial.println("[Settings] NVS saved");
}

uint8_t AIAvatar::findConfiguredWifiNetworkIndex() const {
    if (config_.wifiSsid[0] == '\0') return kNoWifiNetworkIndex;
    for (uint8_t i = 0; i < config_.wifiNetworkCount; ++i) {
        if (strcmp(config_.wifiNetworks[i].ssid, config_.wifiSsid) == 0) return i;
    }
    return kNoWifiNetworkIndex;
}

bool AIAvatar::hasSpeech(const int16_t* samples, size_t sampleCount) const {
    float threshold = 32768.0f * powf(10.0f, config_.vadThresholdDb / 20.0f);
    int64_t thresholdSq = static_cast<int64_t>(threshold * threshold);
    int64_t sum = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        int32_t sample = samples[i];
        sum += sample * sample;
    }
    return sum > thresholdSq * static_cast<int64_t>(sampleCount);
}

uint8_t AIAvatar::nearestVolumeLevel(uint8_t volume) const {
    if (config_.volumeLevelCount == 0) return 0;
    uint8_t bestIndex = 0;
    uint8_t bestDiff = 255;
    for (uint8_t i = 0; i < config_.volumeLevelCount; ++i) {
        uint8_t level = config_.volumeLevels[i];
        uint8_t diff = volume > level ? volume - level : level - volume;
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = i;
        }
    }
    return bestIndex;
}

uint8_t AIAvatar::effectiveSpeakerVolume() const {
    return speakerMuted_ ? 0 : volume_;
}

bool AIAvatar::readMicFrameStatic(int16_t* dest, void* context) {
    auto* self = static_cast<AIAvatar*>(context);
    return self && self->mic_.dequeueFrame(dest);
}

void AIAvatar::clearMicFramesStatic(void* context) {
    auto* self = static_cast<AIAvatar*>(context);
    if (self) self->mic_.clearQueue();
}

void AIAvatar::onAudioChunkStatic(const IncomingAudioChunk& chunk) {
    if (!s_instance || !s_instance->serverProcessing_) return;
    if (!s_instance->speakerReady_) return;
    SpeakerOutput& speaker = s_instance->speaker_;
    if (s_instance->speakerMuted_) {
        if (chunk.pcmData && chunk.pcmSamples > 0) {
            s_instance->visualEffects_.setProcessing(false);
            s_instance->visualEffects_.clearToolPulse();
            s_instance->display_.setDirty();
        }
        return;
    }
    if (chunk.faceName) {
        uint32_t durationMs = chunk.faceDurationSec > 0.0f
                                  ? static_cast<uint32_t>(chunk.faceDurationSec * 1000.0f)
                                  : 0;
        speaker.enqueueFace(static_cast<uint8_t>(FaceController::parseExpression(chunk.faceName)),
                            durationMs);
    }
    if (chunk.pcmData && chunk.pcmSamples > 0) {
        s_instance->visualEffects_.setProcessing(false);
        s_instance->visualEffects_.clearToolPulse();
        s_instance->display_.setDirty();
        if (chunk.sampleRate > 0) {
            speaker.enqueueFormat(chunk.sampleRate, 1, 16);
        }
        speaker.enqueuePcmFrame(chunk.pcmData, chunk.pcmSamples);
    }
}

void AIAvatar::onFinalStatic() {
    if (!s_instance) return;
    s_instance->resetSleepTimer("final");
    s_instance->visualEffects_.setProcessing(false);
    s_instance->visualEffects_.clearToolPulse();
    s_instance->display_.setDirty();
    if (s_instance->config_.fastStartup) s_instance->heavyDeferredResumeMs_ = millis() + 500;
    if (s_instance->speakerReady_ && !s_instance->speakerMuted_ &&
        (s_instance->speaker_.queuedSamples() > 0 || s_instance->speaker_.isPlaying())) {
        s_instance->speaker_.enqueueEnd();
    }
}

void AIAvatar::onFinalTextStatic(const char* responseText, const char* voiceText) {
    if (!s_instance || !s_instance->userFinalCb_) return;
    s_instance->userFinalCb_(responseText, voiceText);
}

void AIAvatar::onStopStatic() {
    if (!s_instance) return;
    s_instance->serverProcessing_ = false;
    s_instance->visualEffects_.setProcessing(false);
    s_instance->visualEffects_.clearToolPulse();
    s_instance->display_.setDirty();
    if (s_instance->config_.fastStartup) s_instance->heavyDeferredResumeMs_ = millis() + 500;
    if (s_instance->speakerReady_) {
        if (s_instance->speaker_.queuedSamples() > 0 || s_instance->speaker_.isPlaying()) {
            s_instance->speaker_.enqueueStop();
        } else {
            s_instance->speaker_.clearQueue();
        }
    }
}

void AIAvatar::onProcessingStatic(bool processing) {
    if (!s_instance) return;
    if (processing) s_instance->resetSleepTimer("processing");
    s_instance->serverProcessing_ = processing;
    s_instance->visualEffects_.setProcessing(processing);
    if (s_instance->config_.fastStartup && !processing) {
        s_instance->heavyDeferredResumeMs_ = millis() + 500;
    }
    s_instance->display_.setDirty();
}

void AIAvatar::onErrorStatic() {
    if (!s_instance) return;
    s_instance->serverProcessing_ = false;
    s_instance->visualEffects_.setProcessing(false);
    s_instance->visualEffects_.clearToolPulse();
    s_instance->visualEffects_.showErrorFlash();
    if (s_instance->config_.fastStartup) {
        s_instance->heavyDeferredResumeMs_ = millis() + 500;
    }
    s_instance->display_.setDirty();
}

void AIAvatar::onStartStatic(const char* text) {
    if (!s_instance) return;
    s_instance->resetSleepTimer("speech start");
    s_instance->interruptPlaybackForNewResponse();
    s_instance->openClaw_.handleResponseStart(text);
    if (s_instance->userStartCb_) s_instance->userStartCb_(text);
}

void AIAvatar::onToolCallStatic(const char* toolName) {
    if (!s_instance) return;
    s_instance->resetSleepTimer("tool call");
    s_instance->visualEffects_.showToolPulse();
    s_instance->display_.setDirty();
    if (!s_instance->openClaw_.handleToolCall(toolName)) {
        s_instance->leds_.startToolPulse();
    }
    if (s_instance->userToolCallCb_) s_instance->userToolCallCb_(toolName);
}

void AIAvatar::onVisionStatic() {
    if (!s_instance) return;
    s_instance->resetSleepTimer("vision");
    s_instance->visualEffects_.setProcessing(false);
    s_instance->visualEffects_.clearToolPulse();
    s_instance->visualEffects_.showVisionFlash();
    s_instance->display_.setDirty();
    s_instance->leds_.startVisionFlash();
    s_instance->visionRequestPending_ = true;
}

void AIAvatar::onAcceptedStatic() {
    if (!s_instance) return;
    s_instance->resetSleepTimer("accepted");
    s_instance->interruptPlaybackForNewResponse();
    s_instance->visualEffects_.clearToolPulse();
    s_instance->visualEffects_.showAccepted();
    s_instance->display_.setDirty();
    s_instance->leds_.startAcceptedFlash();
    if (s_instance->userAcceptedCb_) s_instance->userAcceptedCb_();
}

void AIAvatar::onNadeStatic() {
    if (!s_instance) return;
    s_instance->resetSleepTimer("nade");
    if (s_instance->ws_.isConnected()) {
        struct tm ti;
        time_t now = time(nullptr);
        localtime_r(&now, &ti);

        char invokeBuf[512];
        snprintf(invokeBuf, sizeof(invokeBuf),
                 "%s\n\nCurrent date and time: %04d-%02d-%02d %02d:%02d:%02d",
                 s_instance->config_.nadeInvokePrompt,
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
        s_instance->queueInvokeText(invokeBuf);
    }
    if (s_instance->userNadeCb_) s_instance->userNadeCb_();
}

void AIAvatar::drawOverlayStatic(LGFX_Sprite* canvas) {
    if (!s_instance) return;
    s_instance->visualEffects_.draw(canvas);
    if (s_instance->systemUI_.uiVisible()) {
        s_instance->statusOverlay_.draw(canvas);
    }
    s_instance->openClaw_.draw(canvas);
    if (s_instance->userOverlayCb_) {
        s_instance->userOverlayCb_(canvas);
    }
    s_instance->systemUI_.draw(canvas);
    s_instance->drawVisionPreview(canvas);
}

}  // namespace aiavatar
