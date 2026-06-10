#pragma once

#include "Config.h"

#include <ArduinoJson.h>
#include <M5Unified.h>
#include <cstddef>
#include <cstdint>

namespace aiavatar {

class AIAvatar;
class ResourceProvider;

class ToolActionController {
public:
    static constexpr uint8_t kMaxCategories = 12;
    static constexpr uint8_t kMaxTools = 64;
    static constexpr uint8_t kMaxMotions = 24;
    static constexpr uint8_t kMaxAnimations = 24;
    static constexpr uint8_t kMaxActions = 128;
    static constexpr uint16_t kMaxMotionFrames = 192;
    static constexpr uint8_t kMaxFramesPerMotion = 24;

    ToolActionController();

    void begin(AIAvatar& avatar, const ResourceProvider& resources,
               const char* path = "/tools.json");
    bool reload();
    void update();
    void drawAnimation(LGFX_Sprite* canvas) const;

    void cancel();
    bool execute(const char* toolName);
    bool execute(uint8_t categoryIndex, uint8_t toolIndex);

    bool loaded() const { return loaded_; }
    bool hasTools() const { return toolCount_ > 0; }
    bool running() const { return running_; }
    bool motionRunning() const { return motionRunning_; }

    uint8_t categoryCount() const { return categoryCount_; }
    const char* categoryName(uint8_t index) const;
    uint8_t toolCount(uint8_t categoryIndex) const;
    const char* toolLabel(uint8_t categoryIndex, uint8_t toolIndex) const;
    const char* toolName(uint8_t categoryIndex, uint8_t toolIndex) const;

private:
    enum class ActionType : uint8_t {
        Unknown = 0,
        Motion,
        Audio,
        Animation,
    };

    enum class ActionStartResult : uint8_t {
        Waiting = 0,
        Started,
        Completed,
    };

    struct MotionFrame {
        int16_t yaw;
        int16_t pitch;
        uint16_t speed;
        uint16_t holdMs;
    };

    struct MotionDef {
        char name[32];
        uint16_t frameStart;
        uint8_t frameCount;
    };

    struct AnimationDef {
        char name[32];
        char dir[96];
        char pattern[24];
        uint16_t start;
        uint16_t count;
        uint8_t fps;
        bool cover;
    };

    struct ToolAction {
        ActionType type;
        char value[96];
        uint8_t repeat;
    };

    struct ToolDef {
        char name[40];
        char label[40];
        uint8_t actionStart;
        uint8_t actionCount;
    };

    struct CategoryDef {
        char name[40];
        uint8_t toolStart;
        uint8_t toolCount;
    };

    AIAvatar* avatar_;
    const ResourceProvider* resources_;
    char path_[64];
    bool loaded_;

    CategoryDef categories_[kMaxCategories];
    ToolDef tools_[kMaxTools];
    MotionDef motions_[kMaxMotions];
    AnimationDef animations_[kMaxAnimations];
    ToolAction actions_[kMaxActions];
    MotionFrame motionFrames_[kMaxMotionFrames];

    uint8_t categoryCount_;
    uint8_t toolCount_;
    uint8_t motionCount_;
    uint8_t animationCount_;
    uint8_t actionCount_;
    uint16_t motionFrameCount_;

    bool pending_;
    uint8_t pendingToolIndex_;
    bool running_;
    uint8_t activeToolIndex_;
    uint8_t activeActionOffset_;
    bool actionStarted_;

    bool motionRunning_;
    const MotionFrame* motionFramesActive_;
    uint8_t motionFrameCountActive_;
    uint8_t motionFrameIndex_;
    uint8_t motionRepeatRemaining_;
    uint32_t motionNextMs_;

    bool audioRunning_;
    bool audioDraining_;
    uint8_t* audioData_;
    size_t audioLen_;
    size_t audioOffset_;
    size_t audioEnd_;
    uint32_t audioSampleRate_;
    uint8_t audioChannels_;
    int16_t audioScratch_[kPlaybackChunkSamples];

    bool animationRunning_;
    const AnimationDef* activeAnimation_;
    uint16_t animationFrameIndex_;
    uint32_t animationNextMs_;
    uint8_t* animationFrameData_;
    size_t animationFrameLen_;

    void resetDefinitions();
    void resetRuntime();
    void stopCurrentAction();
    bool loadFromJsonBytes(const uint8_t* data, size_t len);
    void parseMotions(JsonVariantConst root);
    void parseAnimations(JsonVariantConst root);
    void parseCategories(JsonVariantConst root);
    bool parseMotionFrames(JsonArrayConst frames, MotionDef& motion);
    ActionType parseActionType(const char* type) const;

    const ToolDef* findTool(const char* toolName, uint8_t* indexOut = nullptr) const;
    const MotionDef* findMotion(const char* name) const;
    const AnimationDef* findAnimation(const char* name) const;
    const ToolDef* toolAt(uint8_t categoryIndex, uint8_t toolIndex) const;

    void startTool(uint8_t toolIndex);
    void advanceAction();
    ActionStartResult startCurrentAction();
    bool updateCurrentAction();

    ActionStartResult startMotionAction(const ToolAction& action);
    bool updateMotionAction();
    void applyMotionFrame(const MotionFrame& frame);

    ActionStartResult startAudioAction(const ToolAction& action);
    bool updateAudioAction();
    bool parseWavHeader(const uint8_t* data, size_t len, size_t& dataOffset, size_t& dataLen,
                        uint32_t& sampleRate, uint8_t& channels) const;
    void freeAudio();

    ActionStartResult startAnimationAction(const ToolAction& action);
    bool updateAnimationAction();
    bool loadAnimationFrame();
    void freeAnimationFrame();
};

}  // namespace aiavatar
