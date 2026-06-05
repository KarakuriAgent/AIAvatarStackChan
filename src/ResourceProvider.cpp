#include "ResourceProvider.h"

#include <Arduino.h>
#include <SD.h>
#include <cstring>
#include <cstdlib>
#include <pgmspace.h>

namespace aiavatar {

ResourceProvider::ResourceProvider()
    : sdAvailable_(false),
      builtinAssets_(nullptr),
      builtinAssetCount_(0) {}

bool ResourceProvider::beginSD(uint8_t csPin, SPIClass& spi, uint32_t frequency) {
    sdAvailable_ = SD.begin(csPin, spi, frequency);
    return sdAvailable_;
}

void ResourceProvider::setBuiltinAssets(const BuiltinAsset* assets, size_t count) {
    builtinAssets_ = assets;
    builtinAssetCount_ = assets ? count : 0;
}

bool ResourceProvider::exists(const char* path) const {
    if (!path || !path[0]) return false;
    if (sdAvailable_ && SD.exists(path)) return true;
    return findBuiltinAsset(path) != nullptr;
}

bool ResourceProvider::readBytes(const char* path, uint8_t** out, size_t* len) const {
    if (!out || !len) return false;
    *out = nullptr;
    *len = 0;
    if (!path || !path[0]) return false;

    if (sdAvailable_ && readSDBytes(path, out, len)) {
        return true;
    }

    const BuiltinAsset* asset = findBuiltinAsset(path);
    if (asset && readBuiltinBytes(*asset, out, len)) {
        return true;
    }

    return false;
}

bool ResourceProvider::loadConfig(Config& config, const char* path) const {
    if (!path || !path[0]) return false;

    if (sdAvailable_ && SD.exists(path)) {
        File file = SD.open(path, FILE_READ);
        if (!file) {
            Serial.printf("[Resources] failed to open %s\n", path);
            return false;
        }
        bool ok = config.loadFromJson(file);
        file.close();
        return ok;
    }

    const BuiltinAsset* asset = findBuiltinAsset(path);
    if (asset) {
        uint8_t* data = nullptr;
        size_t len = 0;
        if (!readBuiltinBytes(*asset, &data, &len)) return false;
        bool ok = config.loadFromJsonBytes(data, len);
        free(data);
        return ok;
    }

    Serial.printf("[Resources] %s not found, using current config\n", path);
    return false;
}

const BuiltinAsset* ResourceProvider::findBuiltinAsset(const char* path) const {
    if (!path || !builtinAssets_) return nullptr;
    for (size_t i = 0; i < builtinAssetCount_; ++i) {
        const BuiltinAsset& asset = builtinAssets_[i];
        if (asset.path && strcmp(asset.path, path) == 0 && asset.data && asset.len > 0) {
            return &asset;
        }
    }
    return nullptr;
}

bool ResourceProvider::readSDBytes(const char* path, uint8_t** out, size_t* len) const {
    File file = SD.open(path, FILE_READ);
    if (!file) return false;

    size_t fileLen = file.size();
    uint8_t* data = static_cast<uint8_t*>(ps_malloc(fileLen));
    if (!data) data = static_cast<uint8_t*>(malloc(fileLen));
    if (!data) {
        Serial.printf("[Resources] buffer allocation failed: %s (%u bytes)\n",
                      path, static_cast<unsigned>(fileLen));
        file.close();
        return false;
    }

    size_t readLen = file.read(data, fileLen);
    file.close();
    if (readLen != fileLen) {
        Serial.printf("[Resources] read failed: %s\n", path);
        free(data);
        return false;
    }

    *out = data;
    *len = fileLen;
    return true;
}

bool ResourceProvider::readBuiltinBytes(const BuiltinAsset& asset, uint8_t** out, size_t* len) const {
    uint8_t* data = static_cast<uint8_t*>(ps_malloc(asset.len));
    if (!data) data = static_cast<uint8_t*>(malloc(asset.len));
    if (!data) {
        Serial.printf("[Resources] builtin buffer allocation failed: %s (%u bytes)\n",
                      asset.path ? asset.path : "", static_cast<unsigned>(asset.len));
        return false;
    }

    memcpy_P(data, asset.data, asset.len);
    *out = data;
    *len = asset.len;
    return true;
}

}  // namespace aiavatar
