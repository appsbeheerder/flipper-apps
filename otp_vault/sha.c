/*
 * OTPvault - SHA-1, SHA-256, HMAC and PBKDF2 (own implementation, verified by
 * the self-test in otp_vault.c against RFC test vectors).
 * Copyright (c) 2026 Eric M. Kok
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "sha.h"

#include <string.h>

static uint32_t rol32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32u - n));
}

static uint32_t ror32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

static uint32_t load_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void store_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void secure_zero(void* p, size_t len) {
    volatile uint8_t* v = p;
    while(len--) *v++ = 0;
}

//======================================================================================================================
// SHA-1
//======================================================================================================================

void sha1_init(Sha1* s) {
    s->h[0] = 0x67452301u;
    s->h[1] = 0xefcdab89u;
    s->h[2] = 0x98badcfeu;
    s->h[3] = 0x10325476u;
    s->h[4] = 0xc3d2e1f0u;
    s->len = 0;
    s->fill = 0;
}

static void sha1_block(Sha1* s, const uint8_t* p) {
    uint32_t w[80];
    for(int i = 0; i < 16; i++) w[i] = load_be32(p + 4 * i);
    for(int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for(int i = 0; i < 80; i++) {
        uint32_t f, k;
        if(i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999u;
        } else if(i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if(i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    secure_zero(w, sizeof(w));
}

void sha1_update(Sha1* s, const uint8_t* data, size_t len) {
    s->len += len;
    while(len) {
        size_t take = 64u - s->fill;
        if(take > len) take = len;
        memcpy(s->buf + s->fill, data, take);
        s->fill = (uint8_t)(s->fill + take);
        data += take;
        len -= take;
        if(s->fill == 64) {
            sha1_block(s, s->buf);
            s->fill = 0;
        }
    }
}

void sha1_final(Sha1* s, uint8_t out[SHA1_LEN]) {
    uint64_t bits = s->len * 8u;
    s->buf[s->fill++] = 0x80;
    if(s->fill > 56) {
        memset(s->buf + s->fill, 0, 64u - s->fill);
        sha1_block(s, s->buf);
        s->fill = 0;
    }
    memset(s->buf + s->fill, 0, 56u - s->fill);
    for(int i = 0; i < 8; i++) s->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_block(s, s->buf);
    for(int i = 0; i < 5; i++) store_be32(out + 4 * i, s->h[i]);
    secure_zero(s, sizeof(*s));
}

//======================================================================================================================
// SHA-256
//======================================================================================================================

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

void sha256_init(Sha256* s) {
    s->h[0] = 0x6a09e667u;
    s->h[1] = 0xbb67ae85u;
    s->h[2] = 0x3c6ef372u;
    s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu;
    s->h[5] = 0x9b05688cu;
    s->h[6] = 0x1f83d9abu;
    s->h[7] = 0x5be0cd19u;
    s->len = 0;
    s->fill = 0;
}

static void sha256_block(Sha256* s, const uint8_t* p) {
    uint32_t w[64];
    for(int i = 0; i < 16; i++) w[i] = load_be32(p + 4 * i);
    for(int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for(int i = 0; i < 64; i++) {
        uint32_t big1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + big1 + ch + K256[i] + w[i];
        uint32_t big0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = big0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
    secure_zero(w, sizeof(w));
}

void sha256_update(Sha256* s, const uint8_t* data, size_t len) {
    s->len += len;
    while(len) {
        size_t take = 64u - s->fill;
        if(take > len) take = len;
        memcpy(s->buf + s->fill, data, take);
        s->fill = (uint8_t)(s->fill + take);
        data += take;
        len -= take;
        if(s->fill == 64) {
            sha256_block(s, s->buf);
            s->fill = 0;
        }
    }
}

void sha256_final(Sha256* s, uint8_t out[SHA256_LEN]) {
    uint64_t bits = s->len * 8u;
    s->buf[s->fill++] = 0x80;
    if(s->fill > 56) {
        memset(s->buf + s->fill, 0, 64u - s->fill);
        sha256_block(s, s->buf);
        s->fill = 0;
    }
    memset(s->buf + s->fill, 0, 56u - s->fill);
    for(int i = 0; i < 8; i++) s->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_block(s, s->buf);
    for(int i = 0; i < 8; i++) store_be32(out + 4 * i, s->h[i]);
    secure_zero(s, sizeof(*s));
}

//======================================================================================================================
// HMAC
//======================================================================================================================

void hmac_sha1(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* msg,
    size_t msg_len,
    uint8_t out[SHA1_LEN]) {
    uint8_t k[64] = {0};
    uint8_t pad[64];
    uint8_t inner[SHA1_LEN];
    Sha1 s;

    if(key_len > 64) {
        sha1_init(&s);
        sha1_update(&s, key, key_len);
        sha1_final(&s, k);
    } else {
        memcpy(k, key, key_len);
    }

    for(int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha1_init(&s);
    sha1_update(&s, pad, 64);
    sha1_update(&s, msg, msg_len);
    sha1_final(&s, inner);

    for(int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha1_init(&s);
    sha1_update(&s, pad, 64);
    sha1_update(&s, inner, SHA1_LEN);
    sha1_final(&s, out);

    secure_zero(k, sizeof(k));
    secure_zero(pad, sizeof(pad));
    secure_zero(inner, sizeof(inner));
}

void hmac_sha256(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* msg,
    size_t msg_len,
    uint8_t out[SHA256_LEN]) {
    uint8_t k[64] = {0};
    uint8_t pad[64];
    uint8_t inner[SHA256_LEN];
    Sha256 s;

    if(key_len > 64) {
        sha256_init(&s);
        sha256_update(&s, key, key_len);
        sha256_final(&s, k);
    } else {
        memcpy(k, key, key_len);
    }

    for(int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&s);
    sha256_update(&s, pad, 64);
    sha256_update(&s, msg, msg_len);
    sha256_final(&s, inner);

    for(int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&s);
    sha256_update(&s, pad, 64);
    sha256_update(&s, inner, SHA256_LEN);
    sha256_final(&s, out);

    secure_zero(k, sizeof(k));
    secure_zero(pad, sizeof(pad));
    secure_zero(inner, sizeof(inner));
}

//======================================================================================================================
// PBKDF2-HMAC-SHA256 (single output block)
//======================================================================================================================

void pbkdf2_hmac_sha256(
    const uint8_t* password,
    size_t password_len,
    const uint8_t* salt,
    size_t salt_len,
    uint32_t iterations,
    uint8_t out[SHA256_LEN]) {
    uint8_t k[64] = {0};
    uint8_t pad[64];
    uint8_t msg[68];
    uint8_t u[SHA256_LEN];
    uint8_t t[SHA256_LEN];
    Sha256 inner, outer, ctx;

    if(salt_len > 64) salt_len = 64;

    if(password_len > 64) {
        sha256_init(&ctx);
        sha256_update(&ctx, password, password_len);
        sha256_final(&ctx, k);
    } else {
        memcpy(k, password, password_len);
    }

    // The ipad/opad blocks are absorbed once; every iteration then costs only
    // two compression calls.
    for(int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&inner);
    sha256_update(&inner, pad, 64);
    for(int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&outer);
    sha256_update(&outer, pad, 64);

    memcpy(msg, salt, salt_len);
    msg[salt_len] = 0;
    msg[salt_len + 1] = 0;
    msg[salt_len + 2] = 0;
    msg[salt_len + 3] = 1;

    ctx = inner;
    sha256_update(&ctx, msg, salt_len + 4);
    sha256_final(&ctx, u);
    ctx = outer;
    sha256_update(&ctx, u, SHA256_LEN);
    sha256_final(&ctx, u);
    memcpy(t, u, SHA256_LEN);

    for(uint32_t it = 1; it < iterations; it++) {
        ctx = inner;
        sha256_update(&ctx, u, SHA256_LEN);
        sha256_final(&ctx, u);
        ctx = outer;
        sha256_update(&ctx, u, SHA256_LEN);
        sha256_final(&ctx, u);
        for(int i = 0; i < SHA256_LEN; i++) t[i] ^= u[i];
    }

    memcpy(out, t, SHA256_LEN);
    secure_zero(k, sizeof(k));
    secure_zero(pad, sizeof(pad));
    secure_zero(msg, sizeof(msg));
    secure_zero(u, sizeof(u));
    secure_zero(t, sizeof(t));
    secure_zero(&inner, sizeof(inner));
    secure_zero(&outer, sizeof(outer));
    secure_zero(&ctx, sizeof(ctx));
}
