#pragma once

#include "Config.h"

#include <SPI.h>
#include <cstddef>
#include <cstdint>

namespace aiavatar {

struct BuiltinAsset {
    const char* path;
    const uint8_t* data;
    size_t len;
};

class ResourceProvider {
public:
    ResourceProvider();

    bool beginSD(uint8_t csPin, SPIClass& spi = SPI, uint32_t frequency = 25000000);
    void useSD(bool available = true) { sdAvailable_ = available; }
    bool sdAvailable() const { return sdAvailable_; }

    void setBuiltinAssets(const BuiltinAsset* assets, size_t count);

    bool exists(const char* path) const;
    bool readBytes(const char* path, uint8_t** out, size_t* len) const;
    bool loadConfig(Config& config, const char* path = "/config.json") const;

private:
    bool sdAvailable_;
    const BuiltinAsset* builtinAssets_;
    size_t builtinAssetCount_;

    const BuiltinAsset* findBuiltinAsset(const char* path) const;
    bool readSDBytes(const char* path, uint8_t** out, size_t* len) const;
    bool readBuiltinBytes(const BuiltinAsset& asset, uint8_t** out, size_t* len) const;
};

}  // namespace aiavatar
