#pragma once

#include <M5Unified.h>

namespace aiavatar {

class ResourceProvider;

using ScreenOverlayCallback = void (*)(LGFX_Sprite* canvas);

enum class ImageFitMode : uint8_t {
    Contain = 0,
    Cover,
};

struct SpriteLayer {
    LGFX_Sprite* sprite;
    int16_t x;
    int16_t y;
    uint16_t transparentColor;

    bool valid() const { return sprite != nullptr; }
};

class ScreenRenderer {
public:
    static constexpr uint16_t kTransparentColor = 0xF81F;

    ScreenRenderer();

    bool begin(uint8_t rotation = 1, uint8_t brightness = 128);
    bool setRotation(uint8_t rotation);
    uint8_t rotation() const { return rotation_; }
    void setResourceProvider(ResourceProvider* resources) { resources_ = resources; }
    void setImageFitMode(ImageFitMode mode) { imageFitMode_ = mode; }
    ImageFitMode imageFitMode() const { return imageFitMode_; }
    LGFX_Sprite* loadSprite(const char* path, int w, int h,
                            uint16_t bgColor = TFT_BLACK);
    SpriteLayer loadLayerSprite(const char* path, int w, int h,
                                uint16_t transparentColor = kTransparentColor);

    void setBase(LGFX_Sprite* sprite);
    void setOverlay(LGFX_Sprite* sprite);
    void setOverlay(const SpriteLayer& layer);
    void setOverlay2(const SpriteLayer& layer);
    void onOverlay(ScreenOverlayCallback cb) { overlayCb_ = cb; setDirty(); }
    void update();
    void setDirty() { dirty_ = true; }

    int width() const { return width_; }
    int height() const { return height_; }
    bool ready() const { return canvas_ != nullptr; }

private:
    LGFX_Sprite* canvas_;
    LGFX_Sprite* currentBase_;
    SpriteLayer currentOverlay_;
    SpriteLayer currentOverlay2_;
    ScreenOverlayCallback overlayCb_;
    ResourceProvider* resources_;
    ImageFitMode imageFitMode_;
    bool dirty_;
    uint8_t rotation_;
    int width_;
    int height_;
};

}  // namespace aiavatar
