#include "Config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <cstring>

namespace aiavatar {

namespace {

void loadRgbColor(JsonDocument& doc, const char* key, RgbColor& color) {
    if (!doc[key].is<JsonArray>()) return;
    JsonArray arr = doc[key].as<JsonArray>();
    if (arr.size() < 3) return;
    color.r = arr[0].as<uint8_t>();
    color.g = arr[1].as<uint8_t>();
    color.b = arr[2].as<uint8_t>();
}

}  // namespace

Config::Config()
    : wsPort(443),
      wifiNetworkCount(0),
      micSampleRate(16000),
      micMagnification(16),
      micBufferSamples(1024),
      vadThresholdDb(-40),
      playbackQueueDepth(2048),
      playbackStartThreshold(kPlaybackChunkSamples * 2),
      playbackDrainTimeoutMs(500),
      speakerVolume(80),
      volumeLevelCount(5),
      audioTaskStackSize(8192),
      audioTaskCore(0),
      wsTaskStackSize(8192),
      wsTaskCore(1),
      wsReconnectIntervalMs(5000),
      micTxSlowBackoffMs(500),
      micTxFailBackoffMs(3000),
      keepaliveIntervalMs(1000),
      displayRotation(1),
      displayBrightness(128),
      statusOverlayEnabled(true),
      visionPreviewDurationMs(2000),
      acceptedLedColor{0, 168, 0},
      toolLedColor{140, 0, 140},
      pttMaxSeconds(30),
      pttMinSeconds(0.2f),
      pttHoldThresholdMs(200),
      pitchHome(200),
      stackChanAutoAngleSync(true),
      servoRxPin(2),
      servoTxPin(1),
      servoIdX(1),
      servoIdY(2),
      servoYawOffsetDegree(-5),
      takaoBase(true),
      debugLog(false) {
    wifiSsid[0] = '\0';
    wifiPass[0] = '\0';
    wsHost[0] = '\0';
    strlcpy(wsPath, "/ws", sizeof(wsPath));
    strlcpy(userId, "default", sizeof(userId));
    channel[0] = '\0';
    strlcpy(timezone, "JST-9", sizeof(timezone));
    strlcpy(servoType, "SCS0009", sizeof(servoType));
    strlcpy(nadeInvokePrompt,
            "$The user gently patted your head. React with one very short phrase. "
            "If they pat you too often in a short time, you may sound a little annoyed or shy. "
            "When you are upset or troubled, you may use angry or sorrow as the face.",
            sizeof(nadeInvokePrompt));
    strlcpy(visionInvokePrompt,
            "$I am providing visual context needed for your reply. Do not merely describe what is "
            "visible; respond appropriately as if you saw this image in the current conversation context.",
            sizeof(visionInvokePrompt));
    const uint8_t defaultVolumeLevels[] = {0, 32, 80, 160, 255};
    memcpy(volumeLevels, defaultVolumeLevels, sizeof(defaultVolumeLevels));
    for (uint8_t i = sizeof(defaultVolumeLevels); i < kMaxVolumeLevels; ++i) {
        volumeLevels[i] = 0;
    }
    for (auto& network : wifiNetworks) {
        network.ssid[0] = '\0';
        network.pass[0] = '\0';
        network.name[0] = '\0';
    }
}

bool Config::loadFromSD(const char* path) {
    if (!SD.exists(path)) {
        Serial.printf("[Config] %s not found, using defaults\n", path);
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[Config] failed to open %s\n", path);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[Config] JSON parse error: %s\n", err.c_str());
        return false;
    }

    strlcpy(wifiSsid, doc["wifi_ssid"] | wifiSsid, sizeof(wifiSsid));
    strlcpy(wifiPass, doc["wifi_pass"] | wifiPass, sizeof(wifiPass));
    if (doc["wifi_networks"].is<JsonArray>()) {
        JsonArray networks = doc["wifi_networks"].as<JsonArray>();
        wifiNetworkCount = 0;
        for (JsonObject network : networks) {
            if (wifiNetworkCount >= kMaxWifiNetworks) break;
            const char* ssid = network["ssid"] | "";
            if (!ssid[0]) continue;
            auto& dst = wifiNetworks[wifiNetworkCount++];
            strlcpy(dst.ssid, ssid, sizeof(dst.ssid));
            strlcpy(dst.pass, network["pass"] | "", sizeof(dst.pass));
            strlcpy(dst.name, network["name"] | "", sizeof(dst.name));
        }
        if (wifiSsid[0] == '\0' && wifiNetworkCount > 0) {
            strlcpy(wifiSsid, wifiNetworks[0].ssid, sizeof(wifiSsid));
            strlcpy(wifiPass, wifiNetworks[0].pass, sizeof(wifiPass));
        }
    }
    if (wifiSsid[0] != '\0' && wifiNetworkCount == 0) {
        strlcpy(wifiNetworks[0].ssid, wifiSsid, sizeof(wifiNetworks[0].ssid));
        strlcpy(wifiNetworks[0].pass, wifiPass, sizeof(wifiNetworks[0].pass));
        wifiNetworks[0].name[0] = '\0';
        wifiNetworkCount = 1;
    }
    strlcpy(wsHost, doc["ws_host"] | wsHost, sizeof(wsHost));
    wsPort = doc["ws_port"] | wsPort;
    strlcpy(wsPath, doc["ws_path"] | wsPath, sizeof(wsPath));
    strlcpy(userId, doc["user_id"] | userId, sizeof(userId));
    strlcpy(channel, doc["channel"] | channel, sizeof(channel));
    strlcpy(timezone, doc["timezone"] | timezone, sizeof(timezone));

    micSampleRate = doc["mic_sample_rate"] | micSampleRate;
    micMagnification = doc["mic_magnification"] | micMagnification;
    size_t requestedMicSamples = doc["mic_buffer_samples"] | micBufferSamples;
    if (requestedMicSamples > 0 && requestedMicSamples <= kMicBufferSamplesMax) {
        micBufferSamples = requestedMicSamples;
    }
    vadThresholdDb = doc["vad_threshold_db"] | vadThresholdDb;

    if (doc.containsKey("playback_queue_depth")) {
        playbackQueueDepth = doc["playback_queue_depth"].as<size_t>();
    } else if (doc.containsKey("rbuf_samples")) {
        size_t legacySamples = doc["rbuf_samples"].as<size_t>();
        playbackQueueDepth = (legacySamples + kPlaybackChunkSamples - 1) / kPlaybackChunkSamples;
        if (playbackQueueDepth > 2048) playbackQueueDepth = 2048;
    }
    playbackStartThreshold = doc["start_threshold"] | playbackStartThreshold;
    playbackDrainTimeoutMs = doc["drain_timeout_ms"] | playbackDrainTimeoutMs;
    speakerVolume = doc["speaker_volume"] | speakerVolume;
    if (doc["volume_levels"].is<JsonArray>()) {
        JsonArray levels = doc["volume_levels"].as<JsonArray>();
        uint8_t count = 0;
        for (JsonVariant level : levels) {
            if (count >= kMaxVolumeLevels) break;
            volumeLevels[count++] = level.as<uint8_t>();
        }
        if (count >= 2) {
            volumeLevelCount = count;
        }
    }

    audioTaskStackSize = doc["audio_task_stack_size"] | audioTaskStackSize;
    audioTaskCore = doc["audio_task_core"] | audioTaskCore;
    wsTaskStackSize = doc["ws_task_stack_size"] | wsTaskStackSize;
    wsTaskCore = doc["ws_task_core"] | wsTaskCore;
    wsReconnectIntervalMs = doc["ws_reconnect_interval_ms"] | wsReconnectIntervalMs;
    micTxSlowBackoffMs = doc["mic_tx_slow_backoff_ms"] | micTxSlowBackoffMs;
    micTxFailBackoffMs = doc["mic_tx_fail_backoff_ms"] | micTxFailBackoffMs;
    keepaliveIntervalMs = doc["keepalive_interval_ms"] | keepaliveIntervalMs;
    displayRotation = doc["display_rotation"] | displayRotation;
    displayBrightness = doc["display_brightness"] | displayBrightness;
    statusOverlayEnabled = doc["status_overlay_enabled"] | statusOverlayEnabled;
    visionPreviewDurationMs = doc["vision_preview_duration_ms"] | visionPreviewDurationMs;
    loadRgbColor(doc, "accepted_led_color", acceptedLedColor);
    loadRgbColor(doc, "tool_led_color", toolLedColor);
    pttMaxSeconds = doc["ptt_max_seconds"] | pttMaxSeconds;
    pttMinSeconds = doc["ptt_min_seconds"] | pttMinSeconds;
    pttHoldThresholdMs = doc["ptt_hold_threshold_ms"] | pttHoldThresholdMs;
    pitchHome = doc["pitch_home"] | pitchHome;
    stackChanAutoAngleSync = doc["stackchan_auto_angle_sync"] | stackChanAutoAngleSync;
    strlcpy(servoType, doc["servo_type"] | servoType, sizeof(servoType));
    servoRxPin = doc["servo_rx_pin"] | servoRxPin;
    servoTxPin = doc["servo_tx_pin"] | servoTxPin;
    servoIdX = doc["servo_id_x"] | servoIdX;
    servoIdY = doc["servo_id_y"] | servoIdY;
    servoYawOffsetDegree = doc["servo_yaw_offset_degree"] | servoYawOffsetDegree;
    takaoBase = doc["takao_base"] | takaoBase;
    strlcpy(nadeInvokePrompt, doc["nade_invoke_prompt"] | nadeInvokePrompt,
            sizeof(nadeInvokePrompt));
    strlcpy(visionInvokePrompt, doc["vision_invoke_prompt"] | visionInvokePrompt,
            sizeof(visionInvokePrompt));
    debugLog = doc["debug_log"] | debugLog;

    Serial.printf("[Config] WS: %s:%u%s user=%s\n", wsHost, wsPort, wsPath, userId);
    Serial.printf("[Config] mic=%uHz/%u samples speakerVol=%u\n",
                  micSampleRate, micBufferSamples, speakerVolume);
    return true;
}

}  // namespace aiavatar
