#include "OtaTrust.h"

#include "ResourceProvider.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

namespace aiavatar {

namespace {

constexpr const char* kSignaturePayloadPrefix = "aiavatar-ota-signature-v1";
constexpr size_t kCanonicalPayloadMaxLen = 768;

bool decodeBase64(const char* input, uint8_t** out, size_t* outLen) {
    if (!input || !input[0] || !out || !outLen) return false;
    *out = nullptr;
    *outLen = 0;

    size_t inputLen = strlen(input);
    size_t capacity = (inputLen * 3) / 4 + 4;
    uint8_t* buffer = static_cast<uint8_t*>(malloc(capacity));
    if (!buffer) return false;

    size_t decodedLen = 0;
    int rc = mbedtls_base64_decode(buffer, capacity, &decodedLen,
                                   reinterpret_cast<const unsigned char*>(input),
                                   inputLen);
    if (rc != 0 || decodedLen == 0) {
        free(buffer);
        return false;
    }

    *out = buffer;
    *outLen = decodedLen;
    return true;
}

void sha256Bytes(const char* payload, uint8_t digest[32]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, reinterpret_cast<const unsigned char*>(payload),
                          strlen(payload));
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
}

}  // namespace

OtaTrust::OtaTrust() {
    clear();
}

void OtaTrust::clear() {
    memset(signingKeys_, 0, sizeof(signingKeys_));
    signingKeyCount_ = 0;
}

bool OtaTrust::loadFromJsonBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        Serial.printf("[OtaTrust] JSON parse error: %s\n", err.c_str());
        return false;
    }

    clear();
    if (!doc["ota_signing_public_keys"].is<JsonArray>()) {
        Serial.println("[OtaTrust] ota_signing_public_keys missing");
        return false;
    }

    JsonArray keys = doc["ota_signing_public_keys"].as<JsonArray>();
    for (JsonObject key : keys) {
        if (signingKeyCount_ >= kMaxOtaSigningPublicKeys) break;
        const char* id = key["id"] | "";
        const char* alg = key["alg"] | "es256";
        const char* publicKey = key["public_key"] | "";
        if (!id[0] || !publicKey[0]) continue;

        OtaSigningPublicKey& dst = signingKeys_[signingKeyCount_++];
        strlcpy(dst.id, id, sizeof(dst.id));
        strlcpy(dst.alg, alg, sizeof(dst.alg));
        strlcpy(dst.publicKey, publicKey, sizeof(dst.publicKey));
    }

    Serial.printf("[OtaTrust] signing keys=%u\n", signingKeyCount_);
    return signingKeyCount_ > 0;
}

bool OtaTrust::loadFromBuiltin(const ResourceProvider& resources, const char* path) {
    uint8_t* data = nullptr;
    size_t len = 0;
    if (!resources.readBuiltinBytes(path, &data, &len)) {
        Serial.printf("[OtaTrust] %s not found in firmware assets\n", path ? path : "");
        return false;
    }

    bool ok = loadFromJsonBytes(data, len);
    free(data);
    return ok;
}

bool OtaTrust::verifyManifest(const char* kind,
                              const char* version,
                              const char* releaseDate,
                              const char* url,
                              size_t size,
                              const char* sha256,
                              const char* signatureKeyId,
                              const char* signatureAlg,
                              const char* signature,
                              char* error,
                              size_t errorSize) const {
    if (!hasSigningKeys()) {
        setError(error, errorSize, "OTA署名鍵未設定");
        return false;
    }
    if (!signatureKeyId || !signatureKeyId[0] || !signature || !signature[0]) {
        setError(error, errorSize, "manifest署名なし");
        return false;
    }

    const char* alg = signatureAlg && signatureAlg[0] ? signatureAlg : "es256";
    const OtaSigningPublicKey* key = findSigningKey(signatureKeyId, alg);
    if (!key) {
        setError(error, errorSize, "署名鍵不一致");
        return false;
    }
    if (strcmp(alg, "es256") != 0) {
        setError(error, errorSize, "署名方式未対応");
        return false;
    }

    char payload[kCanonicalPayloadMaxLen];
    if (!buildCanonicalPayload(kind, version, releaseDate, url, size, sha256,
                               payload, sizeof(payload))) {
        setError(error, errorSize, "署名対象長すぎ");
        return false;
    }

    uint8_t digest[32];
    sha256Bytes(payload, digest);

    uint8_t* publicKeyDer = nullptr;
    size_t publicKeyDerLen = 0;
    if (!decodeBase64(key->publicKey, &publicKeyDer, &publicKeyDerLen)) {
        setError(error, errorSize, "公開鍵Base64不正");
        return false;
    }

    uint8_t* signatureDer = nullptr;
    size_t signatureDerLen = 0;
    if (!decodeBase64(signature, &signatureDer, &signatureDerLen)) {
        free(publicKeyDer);
        setError(error, errorSize, "署名Base64不正");
        return false;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, publicKeyDer, publicKeyDerLen);
    if (rc == 0) {
        rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                               signatureDer, signatureDerLen);
    }
    mbedtls_pk_free(&pk);
    free(publicKeyDer);
    free(signatureDer);

    if (rc != 0) {
        setError(error, errorSize, "manifest署名不正");
        return false;
    }
    return true;
}

const OtaSigningPublicKey* OtaTrust::findSigningKey(const char* keyId,
                                                    const char* alg) const {
    if (!keyId || !alg) return nullptr;
    for (uint8_t i = 0; i < signingKeyCount_; ++i) {
        const OtaSigningPublicKey& key = signingKeys_[i];
        if (strcmp(key.id, keyId) == 0 && strcmp(key.alg, alg) == 0) {
            return &key;
        }
    }
    return nullptr;
}

bool OtaTrust::buildCanonicalPayload(const char* kind,
                                     const char* version,
                                     const char* releaseDate,
                                     const char* url,
                                     size_t size,
                                     const char* sha256,
                                     char* out,
                                     size_t outSize) const {
    if (!out || outSize == 0) return false;
    int written = snprintf(out, outSize,
                           "%s\n"
                           "kind=%s\n"
                           "version=%s\n"
                           "release_date=%s\n"
                           "url=%s\n"
                           "size=%llu\n"
                           "sha256=%s\n",
                           kSignaturePayloadPrefix,
                           kind ? kind : "",
                           version ? version : "",
                           releaseDate ? releaseDate : "",
                           url ? url : "",
                           static_cast<unsigned long long>(size),
                           sha256 ? sha256 : "");
    return written > 0 && static_cast<size_t>(written) < outSize;
}

void OtaTrust::setError(char* error, size_t errorSize, const char* message) const {
    if (!error || errorSize == 0) return;
    strlcpy(error, message ? message : "", errorSize);
}

}  // namespace aiavatar
