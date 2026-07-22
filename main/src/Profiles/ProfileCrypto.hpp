#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace opencomfort {

class ProfileCrypto {
public:
    // Computes SHA-256 hex (lowercase, 64 chars + NUL).
    static esp_err_t sha256Hex(const uint8_t* data, size_t len, char outHex[65]);

    // Verifies ECDSA P-256 signature (DER) over message using embedded public key PEM.
    static esp_err_t verifyEcdsaSignature(const uint8_t* message, size_t messageLen,
                                          const uint8_t* signatureDer, size_t signatureLen);

    static esp_err_t verifyEcdsaSignatureHex(const uint8_t* message, size_t messageLen,
                                             const char* signatureHex);
};

}  // namespace opencomfort
