#pragma once

#include <cstddef>
#include <cstdint>

namespace aiavatar {

class ResourceProvider;

static constexpr uint8_t kMaxOtaSigningPublicKeys = 4;

struct OtaSigningPublicKey {
    char id[32];
    char alg[16];
    char publicKey[1024];
};

class OtaTrust {
public:
    OtaTrust();

    void clear();
    bool loadFromJsonBytes(const uint8_t* data, size_t len);
    bool loadFromBuiltin(const ResourceProvider& resources,
                         const char* path = "/ota_trust.json");

    bool hasSigningKeys() const { return signingKeyCount_ > 0; }
    uint8_t signingKeyCount() const { return signingKeyCount_; }

    bool verifyManifest(const char* kind,
                        const char* version,
                        const char* releaseDate,
                        const char* url,
                        size_t size,
                        const char* sha256,
                        const char* signatureKeyId,
                        const char* signatureAlg,
                        const char* signature,
                        char* error,
                        size_t errorSize) const;

private:
    OtaSigningPublicKey signingKeys_[kMaxOtaSigningPublicKeys];
    uint8_t signingKeyCount_;

    const OtaSigningPublicKey* findSigningKey(const char* keyId,
                                              const char* alg) const;
    bool buildCanonicalPayload(const char* kind,
                               const char* version,
                               const char* releaseDate,
                               const char* url,
                               size_t size,
                               const char* sha256,
                               char* out,
                               size_t outSize) const;
    void setError(char* error, size_t errorSize, const char* message) const;
};

}  // namespace aiavatar
