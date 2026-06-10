#include "ToolActionController.h"

#include "AIAvatar.h"
#include "IdleMotionEstimator.h"
#include "ResourceProvider.h"

#include <Arduino.h>
#include <cstring>
#include <strings.h>

namespace aiavatar {

namespace {

constexpr uint16_t kMotionSpeedMin = 50;
constexpr uint16_t kMotionSpeedMax = 2000;
constexpr uint16_t kMotionHoldMinMs = 20;
constexpr uint16_t kMotionHoldMaxMs = 5000;
constexpr uint8_t kAnimationFpsMin = 1;
constexpr uint8_t kAnimationFpsMax = 15;
constexpr size_t kAudioQueueHighWaterSamples = kPlaybackChunkSamples * 8;
constexpr int16_t kToolMotionPitchUpOffset = -100;
constexpr int16_t kToolMotionPitchDownOffset = 300;

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool fourccEquals(const uint8_t* p, const char* id) {
    return p && id && memcmp(p, id, 4) == 0;
}

bool isJpegSofMarker(uint8_t marker) {
    switch (marker) {
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCD:
        case 0xCE:
        case 0xCF:
            return true;
        default:
            return false;
    }
}

bool readJpegSize(const uint8_t* data, size_t len, uint16_t& width, uint16_t& height) {
    width = 0;
    height = 0;
    if (!data || len < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;

    size_t pos = 2;
    while (pos + 4 <= len) {
        if (data[pos] != 0xFF) {
            ++pos;
            continue;
        }
        while (pos < len && data[pos] == 0xFF) ++pos;
        if (pos >= len) return false;

        uint8_t marker = data[pos++];
        if (marker == 0xD9 || marker == 0xDA) return false;
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) continue;
        if (pos + 2 > len) return false;

        uint16_t segmentLen =
            static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
        pos += 2;
        if (segmentLen < 2) return false;
        size_t payloadLen = segmentLen - 2;
        if (pos + payloadLen > len) return false;

        if (isJpegSofMarker(marker)) {
            if (payloadLen < 5) return false;
            height = static_cast<uint16_t>((data[pos + 1] << 8) | data[pos + 2]);
            width = static_cast<uint16_t>((data[pos + 3] << 8) | data[pos + 4]);
            return width > 0 && height > 0;
        }
        pos += payloadLen;
    }
    return false;
}

uint8_t clampFps(int fps) {
    if (fps < kAnimationFpsMin) return kAnimationFpsMin;
    if (fps > kAnimationFpsMax) return kAnimationFpsMax;
    return static_cast<uint8_t>(fps);
}

uint16_t clampMotionSpeed(int value) {
    if (value < kMotionSpeedMin) return kMotionSpeedMin;
    if (value > kMotionSpeedMax) return kMotionSpeedMax;
    return static_cast<uint16_t>(value);
}

uint16_t clampMotionHoldMs(int value) {
    if (value < kMotionHoldMinMs) return kMotionHoldMinMs;
    if (value > kMotionHoldMaxMs) return kMotionHoldMaxMs;
    return static_cast<uint16_t>(value);
}

int16_t clampToolMotionPitch(int16_t pitchHome, int32_t pitch) {
    int32_t minPitch = static_cast<int32_t>(pitchHome) + kToolMotionPitchUpOffset;
    int32_t maxPitch = static_cast<int32_t>(pitchHome) + kToolMotionPitchDownOffset;
    if (pitch < minPitch) return static_cast<int16_t>(minPitch);
    if (pitch > maxPitch) return static_cast<int16_t>(maxPitch);
    return static_cast<int16_t>(pitch);
}

}  // namespace

ToolActionController::ToolActionController()
    : avatar_(nullptr),
      resources_(nullptr),
      loaded_(false),
      categoryCount_(0),
      toolCount_(0),
      motionCount_(0),
      animationCount_(0),
      actionCount_(0),
      motionFrameCount_(0),
      pending_(false),
      pendingToolIndex_(0),
      running_(false),
      activeToolIndex_(0),
      activeActionOffset_(0),
      actionStarted_(false),
      motionRunning_(false),
      motionFramesActive_(nullptr),
      motionFrameCountActive_(0),
      motionFrameIndex_(0),
      motionRepeatRemaining_(0),
      motionNextMs_(0),
      audioRunning_(false),
      audioDraining_(false),
      audioData_(nullptr),
      audioLen_(0),
      audioOffset_(0),
      audioEnd_(0),
      audioSampleRate_(16000),
      audioChannels_(1),
      animationRunning_(false),
      activeAnimation_(nullptr),
      animationFrameIndex_(0),
      animationNextMs_(0),
      animationFrameData_(nullptr),
      animationFrameLen_(0) {
    path_[0] = '\0';
}

void ToolActionController::begin(AIAvatar& avatar, const ResourceProvider& resources,
                                 const char* path) {
    avatar_ = &avatar;
    resources_ = &resources;
    strlcpy(path_, path && path[0] ? path : "/tools.json", sizeof(path_));
    resetDefinitions();
    resetRuntime();

    uint8_t* data = nullptr;
    size_t len = 0;
    if (!resources_->readBytes(path_, &data, &len)) {
        Serial.printf("[Tools] %s not found; local tools disabled\n", path_);
        return;
    }

    loaded_ = loadFromJsonBytes(data, len);
    free(data);
    Serial.printf("[Tools] load %s categories=%u tools=%u motions=%u animations=%u actions=%u\n",
                  loaded_ ? "ok" : "failed", categoryCount_, toolCount_, motionCount_,
                  animationCount_, actionCount_);
}

bool ToolActionController::reload() {
    if (!avatar_ || !resources_) return false;
    char currentPath[sizeof(path_)];
    strlcpy(currentPath, path_[0] ? path_ : "/tools.json", sizeof(currentPath));
    begin(*avatar_, *resources_, currentPath);
    return loaded_;
}

void ToolActionController::cancel() {
    pending_ = false;
    running_ = false;
    actionStarted_ = false;
    stopCurrentAction();
}

void ToolActionController::resetDefinitions() {
    loaded_ = false;
    categoryCount_ = 0;
    toolCount_ = 0;
    motionCount_ = 0;
    animationCount_ = 0;
    actionCount_ = 0;
    motionFrameCount_ = 0;
    memset(categories_, 0, sizeof(categories_));
    memset(tools_, 0, sizeof(tools_));
    memset(motions_, 0, sizeof(motions_));
    memset(animations_, 0, sizeof(animations_));
    memset(actions_, 0, sizeof(actions_));
    memset(motionFrames_, 0, sizeof(motionFrames_));
}

void ToolActionController::resetRuntime() {
    pending_ = false;
    running_ = false;
    actionStarted_ = false;
    activeToolIndex_ = 0;
    activeActionOffset_ = 0;
    stopCurrentAction();
}

void ToolActionController::stopCurrentAction() {
    motionRunning_ = false;
    motionFramesActive_ = nullptr;
    motionFrameCountActive_ = 0;
    motionFrameIndex_ = 0;
    motionRepeatRemaining_ = 0;
    motionNextMs_ = 0;

    freeAudio();
    freeAnimationFrame();
    animationRunning_ = false;
    activeAnimation_ = nullptr;
    animationFrameIndex_ = 0;
    animationNextMs_ = 0;
}

bool ToolActionController::loadFromJsonBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        Serial.printf("[Tools] JSON parse error: %s\n", err.c_str());
        return false;
    }

    parseMotions(doc.as<JsonVariantConst>());
    parseAnimations(doc.as<JsonVariantConst>());
    parseCategories(doc.as<JsonVariantConst>());
    return true;
}

void ToolActionController::parseMotions(JsonVariantConst root) {
    JsonArrayConst motions = root["motions"].as<JsonArrayConst>();
    if (motions.isNull()) return;

    for (JsonObjectConst item : motions) {
        if (motionCount_ >= kMaxMotions) {
            Serial.println("[Tools] motion limit reached");
            break;
        }

        const char* name = item["name"] | "";
        JsonArrayConst frames = item["frames"].as<JsonArrayConst>();
        if (!name[0] || frames.isNull()) continue;

        MotionDef& motion = motions_[motionCount_];
        strlcpy(motion.name, name, sizeof(motion.name));
        if (!parseMotionFrames(frames, motion)) {
            motion.name[0] = '\0';
            continue;
        }
        ++motionCount_;
    }
}

bool ToolActionController::parseMotionFrames(JsonArrayConst frames, MotionDef& motion) {
    if (motionFrameCount_ >= kMaxMotionFrames) return false;

    uint16_t start = motionFrameCount_;
    uint8_t count = 0;
    for (JsonObjectConst frame : frames) {
        if (count >= kMaxFramesPerMotion || motionFrameCount_ >= kMaxMotionFrames) break;

        MotionFrame& out = motionFrames_[motionFrameCount_++];
        out.yaw = frame["yaw"] | 0;
        out.pitch = frame["pitch"] | 0;
        out.speed = frame["speed"] | 300;
        out.holdMs = frame["hold_ms"] | (frame["holdMs"] | 250);
        ++count;
    }

    if (count == 0) {
        motionFrameCount_ = start;
        return false;
    }

    motion.frameStart = start;
    motion.frameCount = count;
    return true;
}

void ToolActionController::parseAnimations(JsonVariantConst root) {
    JsonArrayConst animations = root["animations"].as<JsonArrayConst>();
    if (animations.isNull()) return;

    for (JsonObjectConst item : animations) {
        if (animationCount_ >= kMaxAnimations) {
            Serial.println("[Tools] animation limit reached");
            break;
        }

        const char* name = item["name"] | "";
        const char* type = item["type"] | "jpeg_sequence";
        const char* dir = item["dir"] | "";
        if (!name[0] || !dir[0] || strcasecmp(type, "jpeg_sequence") != 0) continue;

        AnimationDef& animation = animations_[animationCount_++];
        strlcpy(animation.name, name, sizeof(animation.name));
        strlcpy(animation.dir, dir, sizeof(animation.dir));
        strlcpy(animation.pattern, item["pattern"] | "%03u.jpg", sizeof(animation.pattern));
        animation.start = item["start"] | 0;
        animation.count = item["count"] | 0;
        animation.fps = clampFps(item["fps"] | 8);
        const char* fit = item["fit"] | "contain";
        animation.cover = strcasecmp(fit, "cover") == 0;
        if (animation.count == 0) {
            --animationCount_;
        }
    }
}

void ToolActionController::parseCategories(JsonVariantConst root) {
    JsonArrayConst categories = root["categories"].as<JsonArrayConst>();
    if (categories.isNull()) return;

    for (JsonObjectConst categoryJson : categories) {
        if (categoryCount_ >= kMaxCategories) {
            Serial.println("[Tools] category limit reached");
            break;
        }

        const char* categoryName = categoryJson["name"] | "";
        JsonArrayConst toolsJson = categoryJson["tools"].as<JsonArrayConst>();
        if (!categoryName[0] || toolsJson.isNull()) continue;

        CategoryDef& category = categories_[categoryCount_];
        strlcpy(category.name, categoryName, sizeof(category.name));
        category.toolStart = toolCount_;
        category.toolCount = 0;

        for (JsonObjectConst toolJson : toolsJson) {
            if (toolCount_ >= kMaxTools) {
                Serial.println("[Tools] tool limit reached");
                break;
            }

            const char* toolName = toolJson["name"] | "";
            if (!toolName[0]) continue;

            JsonArrayConst actionsJson = toolJson["actions"].as<JsonArrayConst>();
            if (actionsJson.isNull()) continue;

            uint8_t actionStart = actionCount_;
            uint8_t localActionCount = 0;
            for (JsonObjectConst actionJson : actionsJson) {
                if (actionCount_ >= kMaxActions) {
                    Serial.println("[Tools] action limit reached");
                    break;
                }

                const char* type = actionJson["type"] | "";
                ActionType actionType = parseActionType(type);
                ToolAction& action = actions_[actionCount_];
                action.type = actionType;
                action.repeat = actionJson["repeat"] | 1;
                if (action.repeat == 0) action.repeat = 1;

                const char* value = "";
                if (actionType == ActionType::Audio) {
                    value = actionJson["path"] | "";
                } else {
                    value = actionJson["name"] | "";
                }
                strlcpy(action.value, value, sizeof(action.value));
                ++actionCount_;
                ++localActionCount;
            }

            if (localActionCount == 0) continue;

            ToolDef& tool = tools_[toolCount_++];
            strlcpy(tool.name, toolName, sizeof(tool.name));
            strlcpy(tool.label, toolJson["label"] | toolName, sizeof(tool.label));
            tool.actionStart = actionStart;
            tool.actionCount = localActionCount;
            ++category.toolCount;
        }

        if (category.toolCount > 0) {
            ++categoryCount_;
        } else {
            category.name[0] = '\0';
        }
    }
}

ToolActionController::ActionType ToolActionController::parseActionType(const char* type) const {
    if (!type) return ActionType::Unknown;
    if (strcasecmp(type, "motion") == 0) return ActionType::Motion;
    if (strcasecmp(type, "audio") == 0) return ActionType::Audio;
    if (strcasecmp(type, "animation") == 0) return ActionType::Animation;
    return ActionType::Unknown;
}

const char* ToolActionController::categoryName(uint8_t index) const {
    return index < categoryCount_ ? categories_[index].name : "";
}

uint8_t ToolActionController::toolCount(uint8_t categoryIndex) const {
    return categoryIndex < categoryCount_ ? categories_[categoryIndex].toolCount : 0;
}

const ToolActionController::ToolDef* ToolActionController::toolAt(uint8_t categoryIndex,
                                                                  uint8_t toolIndex) const {
    if (categoryIndex >= categoryCount_) return nullptr;
    const CategoryDef& category = categories_[categoryIndex];
    if (toolIndex >= category.toolCount) return nullptr;
    uint8_t index = category.toolStart + toolIndex;
    if (index >= toolCount_) return nullptr;
    return &tools_[index];
}

const char* ToolActionController::toolLabel(uint8_t categoryIndex, uint8_t toolIndex) const {
    const ToolDef* tool = toolAt(categoryIndex, toolIndex);
    return tool ? tool->label : "";
}

const char* ToolActionController::toolName(uint8_t categoryIndex, uint8_t toolIndex) const {
    const ToolDef* tool = toolAt(categoryIndex, toolIndex);
    return tool ? tool->name : "";
}

const ToolActionController::ToolDef* ToolActionController::findTool(const char* toolName,
                                                                    uint8_t* indexOut) const {
    if (!toolName || !toolName[0]) return nullptr;
    for (uint8_t i = 0; i < toolCount_; ++i) {
        if (strcmp(tools_[i].name, toolName) != 0) continue;
        if (indexOut) *indexOut = i;
        return &tools_[i];
    }
    return nullptr;
}

const ToolActionController::MotionDef* ToolActionController::findMotion(const char* name) const {
    if (!name || !name[0]) return nullptr;
    for (uint8_t i = 0; i < motionCount_; ++i) {
        if (strcmp(motions_[i].name, name) == 0) return &motions_[i];
    }
    return nullptr;
}

const ToolActionController::AnimationDef* ToolActionController::findAnimation(const char* name) const {
    if (!name || !name[0]) return nullptr;
    for (uint8_t i = 0; i < animationCount_; ++i) {
        if (strcmp(animations_[i].name, name) == 0) return &animations_[i];
    }
    return nullptr;
}

bool ToolActionController::execute(const char* toolName) {
    uint8_t index = 0;
    if (!findTool(toolName, &index)) return false;
    startTool(index);
    return true;
}

bool ToolActionController::execute(uint8_t categoryIndex, uint8_t toolIndex) {
    if (categoryIndex >= categoryCount_) return false;
    const CategoryDef& category = categories_[categoryIndex];
    if (toolIndex >= category.toolCount) return false;
    uint8_t index = category.toolStart + toolIndex;
    if (index >= toolCount_) return false;
    startTool(index);
    return true;
}

void ToolActionController::startTool(uint8_t toolIndex) {
    if (!avatar_ || toolIndex >= toolCount_) return;
    avatar_->resetSleepTimer("tool action");
    avatar_->cancelPlayback();
    if (running_ || actionStarted_) {
        stopCurrentAction();
        running_ = false;
        actionStarted_ = false;
    }
    pendingToolIndex_ = toolIndex;
    pending_ = true;
    Serial.printf("[Tools] queued %s\n", tools_[toolIndex].name);
}

void ToolActionController::update() {
    if (!avatar_) return;

    if (!running_ && pending_) {
        pending_ = false;
        running_ = true;
        activeToolIndex_ = pendingToolIndex_;
        activeActionOffset_ = 0;
        actionStarted_ = false;
        stopCurrentAction();
        Serial.printf("[Tools] start %s\n", tools_[activeToolIndex_].name);
    }

    if (!running_) return;
    if (updateCurrentAction()) {
        advanceAction();
    }
}

void ToolActionController::advanceAction() {
    actionStarted_ = false;
    stopCurrentAction();
    ++activeActionOffset_;
    if (activeToolIndex_ >= toolCount_) {
        running_ = false;
        return;
    }
    const ToolDef& tool = tools_[activeToolIndex_];
    if (activeActionOffset_ >= tool.actionCount) {
        running_ = false;
        Serial.printf("[Tools] complete %s\n", tool.name);
    }
}

bool ToolActionController::updateCurrentAction() {
    if (activeToolIndex_ >= toolCount_) return true;
    const ToolDef& tool = tools_[activeToolIndex_];
    if (activeActionOffset_ >= tool.actionCount) return true;

    if (!actionStarted_) {
        ActionStartResult result = startCurrentAction();
        if (result == ActionStartResult::Waiting) return false;
        if (result == ActionStartResult::Completed) return true;
        actionStarted_ = true;
    }

    const ToolAction& action = actions_[tool.actionStart + activeActionOffset_];
    switch (action.type) {
        case ActionType::Motion:
            return updateMotionAction();
        case ActionType::Audio:
            return updateAudioAction();
        case ActionType::Animation:
            return updateAnimationAction();
        case ActionType::Unknown:
        default:
            return true;
    }
}

ToolActionController::ActionStartResult ToolActionController::startCurrentAction() {
    if (activeToolIndex_ >= toolCount_) return ActionStartResult::Completed;
    const ToolDef& tool = tools_[activeToolIndex_];
    if (activeActionOffset_ >= tool.actionCount) return ActionStartResult::Completed;

    const ToolAction& action = actions_[tool.actionStart + activeActionOffset_];
    switch (action.type) {
        case ActionType::Motion:
            return startMotionAction(action);
        case ActionType::Audio:
            return startAudioAction(action);
        case ActionType::Animation:
            return startAnimationAction(action);
        case ActionType::Unknown:
        default:
            Serial.printf("[Tools] unknown action skipped: %s\n", action.value);
            return ActionStartResult::Completed;
    }
}

ToolActionController::ActionStartResult ToolActionController::startMotionAction(
    const ToolAction& action) {
    if (!avatar_) return ActionStartResult::Completed;
    if (strcasecmp(action.value, "home") == 0) {
        avatar_->motion().goHome();
        return ActionStartResult::Completed;
    }

    const MotionDef* motion = findMotion(action.value);
    if (!motion || motion->frameCount == 0) {
        Serial.printf("[Tools] motion not found: %s\n", action.value);
        return ActionStartResult::Completed;
    }

    motionFramesActive_ = &motionFrames_[motion->frameStart];
    motionFrameCountActive_ = motion->frameCount;
    motionFrameIndex_ = 0;
    motionRepeatRemaining_ = action.repeat > 0 ? action.repeat : 1;
    motionRunning_ = true;
    applyMotionFrame(motionFramesActive_[0]);
    motionNextMs_ = millis() + motionFramesActive_[0].holdMs;
    return ActionStartResult::Started;
}

bool ToolActionController::updateMotionAction() {
    if (!motionRunning_) return true;
    if (static_cast<int32_t>(millis() - motionNextMs_) < 0) return false;

    ++motionFrameIndex_;
    if (motionFrameIndex_ >= motionFrameCountActive_) {
        if (motionRepeatRemaining_ > 1) {
            --motionRepeatRemaining_;
            motionFrameIndex_ = 0;
        } else {
            motionRunning_ = false;
            return true;
        }
    }

    const MotionFrame& frame = motionFramesActive_[motionFrameIndex_];
    applyMotionFrame(frame);
    motionNextMs_ = millis() + frame.holdMs;
    return false;
}

void ToolActionController::applyMotionFrame(const MotionFrame& frame) {
    if (!avatar_) return;
    int16_t yaw = clampIdleMotionYaw(frame.yaw);
    int16_t pitchHome = avatar_->motion().pitchHome();
    int16_t pitch = clampToolMotionPitch(pitchHome, frame.pitch);
    uint16_t speed = clampMotionSpeed(frame.speed);
    avatar_->motion().move(yaw, pitch, speed);
}

ToolActionController::ActionStartResult ToolActionController::startAudioAction(
    const ToolAction& action) {
    if (!avatar_ || !resources_) return ActionStartResult::Completed;
    if (!avatar_->isSpeakerReady()) return ActionStartResult::Waiting;
    if (!action.value[0]) return ActionStartResult::Completed;

    freeAudio();
    if (!resources_->readBytes(action.value, &audioData_, &audioLen_)) {
        Serial.printf("[Tools] audio open failed: %s\n", action.value);
        return ActionStartResult::Completed;
    }

    size_t dataOffset = 0;
    size_t dataLen = 0;
    uint32_t sampleRate = 0;
    uint8_t channels = 0;
    if (!parseWavHeader(audioData_, audioLen_, dataOffset, dataLen, sampleRate, channels)) {
        Serial.printf("[Tools] unsupported WAV: %s\n", action.value);
        freeAudio();
        return ActionStartResult::Completed;
    }

    audioOffset_ = dataOffset;
    audioEnd_ = dataOffset + dataLen;
    audioSampleRate_ = sampleRate;
    audioChannels_ = channels;
    audioRunning_ = true;
    audioDraining_ = false;
    avatar_->speaker().enqueueFormat(audioSampleRate_, 1, 16);
    Serial.printf("[Tools] audio start %s rate=%u channels=%u bytes=%u\n",
                  action.value, audioSampleRate_, audioChannels_,
                  static_cast<unsigned>(dataLen));
    return ActionStartResult::Started;
}

bool ToolActionController::updateAudioAction() {
    if (!avatar_) return true;
    SpeakerOutput& speaker = avatar_->speaker();

    if (audioDraining_) {
        if (speaker.queuedSamples() == 0 && !speaker.isPlaying()) {
            freeAudio();
            return true;
        }
        return false;
    }

    if (!audioRunning_ || !audioData_) return true;
    if (audioOffset_ >= audioEnd_) {
        speaker.enqueueEnd();
        audioRunning_ = false;
        audioDraining_ = true;
        free(audioData_);
        audioData_ = nullptr;
        audioLen_ = 0;
        return false;
    }

    if (speaker.queuedSamples() >= kAudioQueueHighWaterSamples) return false;

    size_t bytesPerFrame = static_cast<size_t>(audioChannels_) * sizeof(int16_t);
    if (bytesPerFrame == 0) {
        freeAudio();
        return true;
    }
    size_t remainingFrames = (audioEnd_ - audioOffset_) / bytesPerFrame;
    size_t frames = remainingFrames > kPlaybackChunkSamples ? kPlaybackChunkSamples : remainingFrames;
    if (frames == 0) return false;

    bool ok = false;
    if (audioChannels_ == 1) {
        const int16_t* samples = reinterpret_cast<const int16_t*>(audioData_ + audioOffset_);
        ok = speaker.enqueuePcmFrame(samples, frames);
    } else {
        const int16_t* samples = reinterpret_cast<const int16_t*>(audioData_ + audioOffset_);
        for (size_t i = 0; i < frames; ++i) {
            int32_t left = samples[i * 2];
            int32_t right = samples[i * 2 + 1];
            audioScratch_[i] = static_cast<int16_t>((left + right) / 2);
        }
        ok = speaker.enqueuePcmFrame(audioScratch_, frames);
    }
    if (ok) audioOffset_ += frames * bytesPerFrame;
    return false;
}

bool ToolActionController::parseWavHeader(const uint8_t* data, size_t len, size_t& dataOffset,
                                          size_t& dataLen, uint32_t& sampleRate,
                                          uint8_t& channels) const {
    if (!data || len < 44) return false;
    if (!fourccEquals(data, "RIFF") || !fourccEquals(data + 8, "WAVE")) return false;

    bool gotFmt = false;
    bool gotData = false;
    uint16_t audioFormat = 0;
    uint16_t bitsPerSample = 0;
    uint16_t channelCount = 0;
    uint32_t rate = 0;

    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t* chunk = data + pos;
        uint32_t chunkSize = readLe32(chunk + 4);
        size_t payload = pos + 8;
        if (payload + chunkSize > len) break;

        if (fourccEquals(chunk, "fmt ")) {
            if (chunkSize < 16) return false;
            audioFormat = readLe16(data + payload);
            channelCount = readLe16(data + payload + 2);
            rate = readLe32(data + payload + 4);
            bitsPerSample = readLe16(data + payload + 14);
            gotFmt = true;
        } else if (fourccEquals(chunk, "data")) {
            dataOffset = payload;
            dataLen = chunkSize;
            gotData = true;
        }

        pos = payload + chunkSize + (chunkSize & 1);
        if (gotFmt && gotData) break;
    }

    if (!gotFmt || !gotData) return false;
    if (audioFormat != 1 || bitsPerSample != 16) return false;
    if (channelCount == 0 || channelCount > 2 || rate == 0) return false;

    channels = static_cast<uint8_t>(channelCount);
    sampleRate = rate;
    return true;
}

void ToolActionController::freeAudio() {
    if (audioData_) {
        free(audioData_);
        audioData_ = nullptr;
    }
    audioRunning_ = false;
    audioDraining_ = false;
    audioLen_ = 0;
    audioOffset_ = 0;
    audioEnd_ = 0;
    audioSampleRate_ = 16000;
    audioChannels_ = 1;
}

ToolActionController::ActionStartResult ToolActionController::startAnimationAction(
    const ToolAction& action) {
    const AnimationDef* animation = findAnimation(action.value);
    if (!animation || animation->count == 0) {
        Serial.printf("[Tools] animation not found: %s\n", action.value);
        return ActionStartResult::Completed;
    }

    freeAnimationFrame();
    activeAnimation_ = animation;
    animationFrameIndex_ = 0;
    animationNextMs_ = 0;
    animationRunning_ = true;
    return ActionStartResult::Started;
}

bool ToolActionController::updateAnimationAction() {
    if (!animationRunning_ || !activeAnimation_) return true;
    uint32_t now = millis();
    if (static_cast<int32_t>(now - animationNextMs_) < 0) return false;

    if (animationFrameIndex_ >= activeAnimation_->count) {
        freeAnimationFrame();
        animationRunning_ = false;
        if (avatar_) avatar_->display().setDirty();
        return true;
    }

    if (loadAnimationFrame()) {
        ++animationFrameIndex_;
        uint32_t frameMs = 1000UL / activeAnimation_->fps;
        if (frameMs == 0) frameMs = 1;
        animationNextMs_ = now + frameMs;
        if (avatar_) avatar_->display().setDirty();
    } else {
        animationRunning_ = false;
        if (avatar_) avatar_->display().setDirty();
        return true;
    }
    return false;
}

bool ToolActionController::loadAnimationFrame() {
    if (!resources_ || !activeAnimation_) return false;

    char frameName[40];
    snprintf(frameName, sizeof(frameName), activeAnimation_->pattern,
             static_cast<unsigned>(activeAnimation_->start + animationFrameIndex_));

    char path[144];
    size_t dirLen = strlen(activeAnimation_->dir);
    const char* sep = (dirLen > 0 && activeAnimation_->dir[dirLen - 1] == '/') ? "" : "/";
    snprintf(path, sizeof(path), "%s%s%s", activeAnimation_->dir, sep, frameName);

    uint8_t* data = nullptr;
    size_t len = 0;
    if (!resources_->readBytes(path, &data, &len)) {
        Serial.printf("[Tools] animation frame missing: %s\n", path);
        return false;
    }

    freeAnimationFrame();
    animationFrameData_ = data;
    animationFrameLen_ = len;
    return true;
}

void ToolActionController::freeAnimationFrame() {
    if (animationFrameData_) {
        free(animationFrameData_);
        animationFrameData_ = nullptr;
    }
    animationFrameLen_ = 0;
}

void ToolActionController::drawAnimation(LGFX_Sprite* canvas) const {
    if (!canvas || !animationRunning_ || !animationFrameData_ || animationFrameLen_ == 0) return;

    canvas->fillSprite(TFT_BLACK);
    uint16_t imgW = 0;
    uint16_t imgH = 0;
    if (!readJpegSize(animationFrameData_, animationFrameLen_, imgW, imgH)) {
        canvas->drawJpg(animationFrameData_, animationFrameLen_, 0, 0,
                        canvas->width(), canvas->height());
        return;
    }

    const int canvasW = canvas->width();
    const int canvasH = canvas->height();
    float scaleX = static_cast<float>(canvasW) / imgW;
    float scaleY = static_cast<float>(canvasH) / imgH;
    float scale = activeAnimation_ && activeAnimation_->cover
                      ? (scaleX > scaleY ? scaleX : scaleY)
                      : (scaleX < scaleY ? scaleX : scaleY);
    if (scale <= 0.0f) scale = 1.0f;

    int scaledW = static_cast<int>(imgW * scale + 0.5f);
    int scaledH = static_cast<int>(imgH * scale + 0.5f);
    int x = (canvasW - scaledW) / 2;
    int y = (canvasH - scaledH) / 2;
    int offX = 0;
    int offY = 0;
    if (x < 0) {
        offX = -x;
        x = 0;
    }
    if (y < 0) {
        offY = -y;
        y = 0;
    }

    canvas->drawJpg(animationFrameData_, animationFrameLen_, x, y,
                    canvasW, canvasH, offX, offY, scale, scale);
}

}  // namespace aiavatar
