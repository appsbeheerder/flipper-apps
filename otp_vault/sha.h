/*
 * OTPvault - SHA-1, SHA-256, HMAC and PBKDF2 (own implementation, verified by
 * the self-test in otp_vault.c against RFC test vectors).
 * Copyright (c) 2026 Eric M. Kok
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define SHA1_LEN 20
#define SHA256_LEN 32

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    uint8_t fill;
} Sha1;

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    uint8_t fill;
} Sha256;

void sha1_init(Sha1* s);
void sha1_update(Sha1* s, const uint8_t* data, size_t len);
void sha1_final(Sha1* s, uint8_t out[SHA1_LEN]);

void sha256_init(Sha256* s);
void sha256_update(Sha256* s, const uint8_t* data, size_t len);
void sha256_final(Sha256* s, uint8_t out[SHA256_LEN]);

void hmac_sha1(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* msg,
    size_t msg_len,
    uint8_t out[SHA1_LEN]);

void hmac_sha256(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* msg,
    size_t msg_len,
    uint8_t out[SHA256_LEN]);

// PBKDF2-HMAC-SHA256, one 32-byte output block.
void pbkdf2_hmac_sha256(
    const uint8_t* password,
    size_t password_len,
    const uint8_t* salt,
    size_t salt_len,
    uint32_t iterations,
    uint8_t out[SHA256_LEN]);

// Overwrites memory in a way the compiler may not optimise away.
void secure_zero(void* p, size_t len);
