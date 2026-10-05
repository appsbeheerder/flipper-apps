/*
 * OTPvault - Base32 and TOTP (RFC 4226 / RFC 6238)
 * Copyright (c) 2026 Eric M. Kok
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

typedef enum {
    OtpSha1 = 0,
    OtpSha256 = 1,
} OtpAlgo;

// Decodes RFC 4648 Base32 (case-insensitive; spaces, '-' and '=' ignored).
// Returns the number of bytes written, or -1 on an invalid character or when
// `max` is too small.
int base32_decode(const char* in, uint8_t* out, size_t max);

// Returns the TOTP code, already reduced to `digits` digits.
uint32_t totp_code(
    OtpAlgo algo,
    const uint8_t* secret,
    size_t secret_len,
    uint64_t unix_time,
    uint32_t period,
    uint8_t digits);
