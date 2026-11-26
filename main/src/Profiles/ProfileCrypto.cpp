#include "src/Profiles/ProfileCrypto.hpp"

#include <cstdio>
#include <cstring>

extern "C" {
#include "esp_log.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "ProfileCrypto";

// ECDSA P-256 public key used to verify remotely downloaded profiles.
// The private key is the OPENCOMFORT_PROFILE_SIGNING_KEY secret of the profiles workflow;
// it must never be flashed or committed.
constexpr const char kProfilePublicKeyPem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE1UhmP3NgB5+6Woa/WSbsiYaobnU6\n"
    "YcMF11K/QbCrsu3Sr5F6K9A5G0S6sG78EFzT0bhp8/CeSskNCxhhXgsMzQ==\n"
    "-----END PUBLIC KEY-----\n";

int hexNibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

}  // namespace

esp_err_t ProfileCrypto::sha256Hex(const uint8_t* data, size_t len, char outHex[65])
{
    if (data == nullptr || outHex == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t digest[32] = {};
    if (mbedtls_sha256(data, len, digest, 0) != 0) {
        return ESP_FAIL;
    }
    for (size_t i = 0; i < sizeof(digest); ++i) {
        std::snprintf(outHex + (i * 2), 3, "%02x", digest[i]);
    }
    outHex[64] = '\0';
    return ESP_OK;
}

esp_err_t ProfileCrypto::verifyEcdsaSignature(const uint8_t* message, size_t messageLen,
                                              const uint8_t* signatureDer, size_t signatureLen)
{
    if (message == nullptr || signatureDer == nullptr || messageLen == 0 || signatureLen == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(
        &pk, reinterpret_cast<const unsigned char*>(kProfilePublicKeyPem),
        sizeof(kProfilePublicKeyPem));
    if (ret != 0) {
        ESP_LOGE(kTag, "Public key parse failed: -0x%04x", -ret);
        mbedtls_pk_free(&pk);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t hash[32] = {};
    ret = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), message, messageLen, hash);
    if (ret != 0) {
        mbedtls_pk_free(&pk);
        return ESP_FAIL;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), signatureDer, signatureLen);
    mbedtls_pk_free(&pk);
    if (ret != 0) {
        ESP_LOGW(kTag, "Signature verify failed: -0x%04x", -ret);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t ProfileCrypto::verifyEcdsaSignatureHex(const uint8_t* message, size_t messageLen,
                                                 const char* signatureHex)
{
    if (signatureHex == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t hexLen = std::strlen(signatureHex);
    if (hexLen == 0 || (hexLen % 2) != 0 || hexLen > 256) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t der[128] = {};
    const size_t derLen = hexLen / 2;
    if (derLen > sizeof(der)) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < derLen; ++i) {
        const int hi = hexNibble(signatureHex[i * 2]);
        const int lo = hexNibble(signatureHex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        der[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return verifyEcdsaSignature(message, messageLen, der, derLen);
}

}  // namespace opencomfort
