/*
 * OTPvault - Base32 and TOTP (RFC 4226 / RFC 6238)
 * Copyright (c) 2026 Eric M. Kok
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "otp.h"
#include "sha.h"

int base32_decode(const char* in, uint8_t* out, size_t max) {
    uint32_t buffer = 0;
    int bits = 0;
    size_t n = 0;

    for(; *in; in++) {
        char c = *in;
        if(c == ' ' || c == '-' || c == '=') continue;
        uint32_t v;
        if(c >= 'A' && c <= 'Z') {
            v = (uint32_t)(c - 'A');
        } else if(c >= 'a' && c <= 'z') {
            v = (uint32_t)(c - 'a');
        } else if(c >= '2' && c <= '7') {
            v = (uint32_t)(c - '2') + 26u;
        } else {
            return -1;
        }
        buffer = (buffer << 5) | v;
        bits += 5;
        if(bits >= 8) {
            bits -= 8;
            if(n >= max) return -1;
            out[n++] = (uint8_t)(buffer >> bits);
        }
    }
    return (int)n;
}

uint32_t totp_code(
    OtpAlgo algo,
    const uint8_t* secret,
    size_t secret_len,
    uint64_t unix_time,
    uint32_t period,
    uint8_t digits) {
    uint8_t msg[8];
    uint8_t mac[SHA256_LEN];
    size_t mac_len;

    uint64_t counter = unix_time / period;
    for(int i = 0; i < 8; i++) msg[i] = (uint8_t)(counter >> (56 - 8 * i));

    if(algo == OtpSha256) {
        hmac_sha256(secret, secret_len, msg, sizeof(msg), mac);
        mac_len = SHA256_LEN;
    } else {
        hmac_sha1(secret, secret_len, msg, sizeof(msg), mac);
        mac_len = SHA1_LEN;
    }

    unsigned offset = mac[mac_len - 1] & 0x0fu;
    uint32_t bin = ((uint32_t)(mac[offset] & 0x7fu) << 24) | ((uint32_t)mac[offset + 1] << 16) |
                   ((uint32_t)mac[offset + 2] << 8) | (uint32_t)mac[offset + 3];

    uint32_t mod = 1;
    for(uint8_t i = 0; i < digits; i++) mod *= 10u;

    secure_zero(mac, sizeof(mac));
    return bin % mod;
}
