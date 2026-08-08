#include "secure_store.h"

#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

// Upper bound on the secrets this is used for (WIFI_PASSWORD_MAX_LEN /
// MQTT_PASSWORD_MAX_LEN in device_config.h are both 64) — not read from
// there to avoid a header dependency in a module that has nothing else to
// do with device_config's fields; kept in sync by the shared static_assert
// wherever this is called instead.
static const size_t kSecretPlainMaxLen = 64;
static const size_t kAesBlockLen = 16;
// PKCS7 always adds a full padding block when the input is already a
// multiple of the block size, hence the +1 block here rather than rounding
// kSecretPlainMaxLen up (which would add zero padding in that exact case).
static const size_t kCiphertextMaxLen = kAesBlockLen + ((kSecretPlainMaxLen / kAesBlockLen) + 1) * kAesBlockLen;
static const size_t kPayloadMaxLen = kAesBlockLen + kCiphertextMaxLen; // IV || ciphertext

// A fixed application-specific value folded into the key derivation so the
// key isn't *purely* a function of the eFuse MAC (which is also printed on
// some boards' silkscreen/readable over the debug port) — anyone reversing
// the firmware learns this, but someone who's only recovered the MAC (e.g.
// by sniffing WiFi traffic, without dumping the flash) isn't handed the key
// for free either. Not meant to survive firmware decompilation, only to
// avoid a single trivially-obtainable value being the entire key.
static const char *kPepper = "OpenParcelBox-secure-store-v1";

// AES-256 key = SHA-256(eFuse MAC || pepper). Derived fresh each call rather
// than cached — this runs at most a couple of times per boot (device_config
// load/save), not worth holding key material in RAM any longer than needed.
static void deriveKey(uint8_t key[32]) {
    uint64_t mac = ESP.getEfuseMac();

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0 /* SHA-256, not SHA-224 */);
    mbedtls_sha256_update_ret(&ctx, reinterpret_cast<const uint8_t *>(&mac), sizeof(mac));
    mbedtls_sha256_update_ret(&ctx, reinterpret_cast<const uint8_t *>(kPepper), strlen(kPepper));
    mbedtls_sha256_finish_ret(&ctx, key);
    mbedtls_sha256_free(&ctx);
}

String encryptSecret(const char *plain) {
    if (!plain || !plain[0]) {
        return String("");
    }

    size_t plainLen = strlen(plain);
    if (plainLen > kSecretPlainMaxLen) {
        return String(""); // refuse rather than silently truncate a secret
    }

    // PKCS7-pad into a working buffer sized to the worst case.
    uint8_t padded[kCiphertextMaxLen];
    size_t paddedLen = ((plainLen / kAesBlockLen) + 1) * kAesBlockLen;
    uint8_t padValue = paddedLen - plainLen;
    memcpy(padded, plain, plainLen);
    memset(padded + plainLen, padValue, padValue);

    uint8_t iv[kAesBlockLen];
    esp_fill_random(iv, sizeof(iv));
    uint8_t ivWorking[kAesBlockLen]; // mbedtls advances the IV it's given in place
    memcpy(ivWorking, iv, sizeof(iv));

    uint8_t key[32];
    deriveKey(key);

    uint8_t ciphertext[kCiphertextMaxLen];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 256);
    int rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, ivWorking, padded, ciphertext);
    mbedtls_aes_free(&aes);
    memset(key, 0, sizeof(key));
    if (rc != 0) {
        return String("");
    }

    uint8_t payload[kPayloadMaxLen];
    memcpy(payload, iv, kAesBlockLen);
    memcpy(payload + kAesBlockLen, ciphertext, paddedLen);
    size_t payloadLen = kAesBlockLen + paddedLen;

    // base64 expands by ~4/3 — MBEDTLS_BASE64_ENCODED_OUT_SIZE takes care of
    // rounding that (and the trailing '=' padding/null terminator) correctly.
    unsigned char encoded[((kPayloadMaxLen + 2) / 3) * 4 + 4];
    size_t encodedLen = 0;
    if (mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLen, payload, payloadLen) != 0) {
        return String("");
    }
    encoded[encodedLen] = '\0';

    return String(reinterpret_cast<const char *>(encoded));
}

bool decryptSecret(const char *encoded, char *outPlain, size_t outCap) {
    if (outCap == 0) {
        return false;
    }
    outPlain[0] = '\0';

    if (!encoded || !encoded[0]) {
        return true; // "no password set" round-trips as itself
    }

    uint8_t payload[kPayloadMaxLen];
    size_t payloadLen = 0;
    if (mbedtls_base64_decode(payload, sizeof(payload), &payloadLen,
                               reinterpret_cast<const unsigned char *>(encoded), strlen(encoded)) != 0) {
        return false; // corrupt/truncated base64, or simply too long to be ours
    }
    if (payloadLen <= kAesBlockLen || (payloadLen - kAesBlockLen) % kAesBlockLen != 0) {
        return false; // not IV + a whole number of AES blocks
    }

    const uint8_t *iv = payload;
    const uint8_t *ciphertext = payload + kAesBlockLen;
    size_t ciphertextLen = payloadLen - kAesBlockLen;

    uint8_t ivWorking[kAesBlockLen];
    memcpy(ivWorking, iv, kAesBlockLen);

    uint8_t key[32];
    deriveKey(key);

    uint8_t plain[kCiphertextMaxLen];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 256);
    int rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ciphertextLen, ivWorking, ciphertext, plain);
    mbedtls_aes_free(&aes);
    memset(key, 0, sizeof(key));
    if (rc != 0) {
        return false;
    }

    uint8_t padValue = plain[ciphertextLen - 1];
    if (padValue == 0 || padValue > kAesBlockLen || padValue > ciphertextLen) {
        return false; // not valid PKCS7 padding -> wrong key (wrong board) or corrupt data
    }
    for (size_t i = 0; i < padValue; i++) {
        if (plain[ciphertextLen - 1 - i] != padValue) {
            return false;
        }
    }

    size_t plainLen = ciphertextLen - padValue;
    if (plainLen >= outCap) {
        return false; // shouldn't happen for our own output, but never overrun the caller's buffer
    }
    memcpy(outPlain, plain, plainLen);
    outPlain[plainLen] = '\0';
    return true;
}
