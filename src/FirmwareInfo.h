#pragma once

#if __has_include("FirmwareInfoGenerated.h")
#include "FirmwareInfoGenerated.h"
#endif

#ifndef AIAVATAR_FIRMWARE_VERSION
#define AIAVATAR_FIRMWARE_VERSION "0.1.0"
#endif

#ifndef AIAVATAR_FIRMWARE_RELEASE_DATE
#define AIAVATAR_FIRMWARE_RELEASE_DATE "2026-06-06"
#endif

#ifndef AIAVATAR_OTA_MANIFEST_URL
#define AIAVATAR_OTA_MANIFEST_URL ""
#endif

#ifndef AIAVATAR_TOOL_MANIFEST_URL
#define AIAVATAR_TOOL_MANIFEST_URL ""
#endif

namespace aiavatar {

static constexpr const char* kFirmwareVersion = AIAVATAR_FIRMWARE_VERSION;
static constexpr const char* kFirmwareReleaseDate = AIAVATAR_FIRMWARE_RELEASE_DATE;
static constexpr const char* kOtaManifestUrl = AIAVATAR_OTA_MANIFEST_URL;
static constexpr const char* kToolManifestUrl = AIAVATAR_TOOL_MANIFEST_URL;

}  // namespace aiavatar
