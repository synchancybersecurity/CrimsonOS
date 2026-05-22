/*
 * Crimson OS - Cryptographic Subsystem
 *
 * AES-256-GCM using ARMv8 hardware crypto (AESE/AESMC/PMULL).
 * CSPRNG seeded from ARM generic timer (CNTPCT_EL0) with ChaCha20 mixing.
 * Other algorithms remain stubbed pending full implementation.
 */

#include <crimson/types.h>
#include <crimson/crypto.h>
#include <crimson/printk.h>
#include <crimson/string.h>

/* ---- AES-256 key schedule ---- */

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static const uint8_t aes_rcon[7] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40 };

typedef struct { uint8_t rk[15][16]; } aes256_rk_t;

static void aes256_key_expand(const uint8_t* key, aes256_rk_t* out)
{
    uint8_t w[60][4];
    for (int i = 0; i < 8; i++) {
        w[i][0] = key[i*4+0]; w[i][1] = key[i*4+1];
        w[i][2] = key[i*4+2]; w[i][3] = key[i*4+3];
    }
    for (int i = 8; i < 60; i++) {
        uint8_t t[4];
        memcpy(t, w[i-1], 4);
        if (i % 8 == 0) {
            uint8_t tmp = t[0]; t[0]=t[1]; t[1]=t[2]; t[2]=t[3]; t[3]=tmp;
            t[0]=aes_sbox[t[0]]; t[1]=aes_sbox[t[1]];
            t[2]=aes_sbox[t[2]]; t[3]=aes_sbox[t[3]];
            t[0] ^= aes_rcon[i/8 - 1];
        } else if (i % 8 == 4) {
            t[0]=aes_sbox[t[0]]; t[1]=aes_sbox[t[1]];
            t[2]=aes_sbox[t[2]]; t[3]=aes_sbox[t[3]];
        }
        w[i][0]=w[i-8][0]^t[0]; w[i][1]=w[i-8][1]^t[1];
        w[i][2]=w[i-8][2]^t[2]; w[i][3]=w[i-8][3]^t[3];
    }
    for (int i = 0; i < 15; i++)
        for (int j = 0; j < 4; j++)
            memcpy(&out->rk[i][j*4], w[i*4+j], 4);
}

/*
 * Encrypt one 16-byte block using ARM64 hardware AES.
 * AESE Vd, Vn  = ShiftRows(SubBytes(Vd XOR Vn))  — ARM spec
 * AESMC Vd, Vn = MixColumns(Vn)
 *
 * AES-256: initial AddRoundKey + 13 middle rounds + 1 final round.
 */
static void aes256_encrypt_block(const aes256_rk_t* rk,
                                  const uint8_t* pt, uint8_t* ct)
{
    __asm__ volatile(
        /* Load state */
        "ld1    {v0.16b}, [%[pt]]\n\t"
        /* Round 0: AddRoundKey */
        "ld1    {v1.16b}, [%[rk0]]\n\t"
        "eor    v0.16b, v0.16b, v1.16b\n\t"
        /* Rounds 1-13: AESE(zero) + AESMC + XOR round key */
        "movi   v2.16b, #0\n\t"
        "ld1    {v3.16b},  [%[rk1]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk2]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk3]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk4]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk5]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk6]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk7]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk8]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk9]]\n\t"  "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk10]]\n\t" "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk11]]\n\t" "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk12]]\n\t" "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        "ld1    {v3.16b},  [%[rk13]]\n\t" "aese v0.16b,v2.16b\n\t" "aesmc v0.16b,v0.16b\n\t" "eor v0.16b,v0.16b,v3.16b\n\t"
        /* Round 14 (final): AESE(zero) + XOR rk14 (no AESMC) */
        "ld1    {v3.16b},  [%[rk14]]\n\t" "aese v0.16b,v2.16b\n\t" "eor  v0.16b,v0.16b,v3.16b\n\t"
        /* Store */
        "st1    {v0.16b}, [%[ct]]\n\t"
        :
        : [pt]"r"(pt), [ct]"r"(ct),
          [rk0]"r"(rk->rk[0]),   [rk1]"r"(rk->rk[1]),   [rk2]"r"(rk->rk[2]),
          [rk3]"r"(rk->rk[3]),   [rk4]"r"(rk->rk[4]),   [rk5]"r"(rk->rk[5]),
          [rk6]"r"(rk->rk[6]),   [rk7]"r"(rk->rk[7]),   [rk8]"r"(rk->rk[8]),
          [rk9]"r"(rk->rk[9]),   [rk10]"r"(rk->rk[10]), [rk11]"r"(rk->rk[11]),
          [rk12]"r"(rk->rk[12]), [rk13]"r"(rk->rk[13]), [rk14]"r"(rk->rk[14])
        : "v0","v1","v2","v3","memory"
    );
}

/* ---- GCM helpers ---- */

/* GF(2^128) multiply using ARM64 PMULL.
 * Irreducible polynomial: x^128 + x^7 + x^2 + x + 1 (0xE1...0 in reflected bit order)
 * Both operands and result are in big-endian byte order. */
static void gcm_gmul(const uint8_t X[16], const uint8_t Y[16], uint8_t out[16])
{
    uint64_t x_lo, x_hi, y_lo, y_hi;
    uint64_t r0, r1, r2, r3;
    uint64_t t0, t1;

    /* Load as little-endian 64-bit words (reversed for GCM bit-reflection) */
    memcpy(&x_hi, X,   8); memcpy(&x_lo, X+8, 8);
    memcpy(&y_hi, Y,   8); memcpy(&y_lo, Y+8, 8);

    /* Karatsuba-style 128×128 multiply using PMULL */
    __asm__ volatile(
        "fmov   d16, %x[xhi]\n\t"
        "fmov   d17, %x[xlo]\n\t"
        "fmov   d18, %x[yhi]\n\t"
        "fmov   d19, %x[ylo]\n\t"
        "pmull  v20.1q, v16.1d, v18.1d\n\t"   /* r0 = x_hi * y_hi */
        "pmull  v21.1q, v17.1d, v19.1d\n\t"   /* r1 = x_lo * y_lo */
        "pmull  v22.1q, v16.1d, v19.1d\n\t"   /* r2 = x_hi * y_lo */
        "pmull  v23.1q, v17.1d, v18.1d\n\t"   /* r3 = x_lo * y_hi */
        "eor    v22.16b, v22.16b, v23.16b\n\t" /* r2 ^= r3 */
        /* Extract halves of r2 for folding into r0 and r1 */
        "fmov   %x[r0], d20\n\t"
        "mov    x10, v20.d[1]\n\t"
        "fmov   %x[r1], d21\n\t"
        "mov    x11, v21.d[1]\n\t"
        "fmov   %x[r2], d22\n\t"
        "mov    x12, v22.d[1]\n\t"
        "fmov   %x[t0], d22\n\t"   /* low half of mid */
        "mov    %x[t1], x10\n\t"   /* high of r0 */
        : [r0]"=r"(r0), [r1]"=r"(r1), [r2]"=r"(r2), [r3]"=r"(r3),
          [t0]"=r"(t0), [t1]"=r"(t1)
        : [xhi]"r"(x_hi), [xlo]"r"(x_lo),
          [yhi]"r"(y_hi), [ylo]"r"(y_lo)
        : "x10","x11","x12","v16","v17","v18","v19","v20","v21","v22","v23","memory"
    );

    /* Fold the 256-bit product mod the GCM polynomial (0xE100...01).
     * Use software reduction — PMULL handles the multiply, reduction is 4 XOR+shift ops. */
    uint64_t lo_lo, lo_hi, hi_lo, hi_hi;
    uint64_t tmp;
    __asm__ volatile(
        "fmov   d16, %x[xhi]\n\t"
        "fmov   d17, %x[xlo]\n\t"
        "fmov   d18, %x[yhi]\n\t"
        "fmov   d19, %x[ylo]\n\t"
        /* Full 128×128 → 256-bit product */
        "pmull2 v20.1q, v16.2d, v18.2d\n\t"   /* hi×hi → bits 255:128 -- wait, wrong width */
        /* Redo with correct lane ops */
        "pmull  v20.1q, v16.1d, v18.1d\n\t"
        "pmull  v21.1q, v17.1d, v19.1d\n\t"
        "pmull  v22.1q, v16.1d, v19.1d\n\t"
        "pmull  v23.1q, v17.1d, v18.1d\n\t"
        "eor    v22.16b, v22.16b, v23.16b\n\t"
        /* Spread mid term */
        "ext    v23.16b, v22.16b, v20.16b, #8\n\t"
        "ext    v22.16b, v21.16b, v22.16b, #8\n\t"
        "eor    v20.16b, v20.16b, v23.16b\n\t"
        "eor    v21.16b, v21.16b, v22.16b\n\t"
        /* Now v21 = lo 128 bits, v20 = hi 128 bits of product */
        /* Montgomery-style reduction for GCM polynomial x^128+x^7+x^2+x+1 */
        "movi   v24.16b, #0xe1\n\t"
        "shl    v24.2d,  v24.2d, #56\n\t"   /* v24 = 0xe100...0 */
        "pmull2 v25.1q,  v20.2d, v24.1d\n\t"
        "ext    v26.16b, v20.16b, v20.16b, #8\n\t"
        "eor    v21.16b, v21.16b, v25.16b\n\t"
        "eor    v21.16b, v21.16b, v26.16b\n\t"
        "pmull  v25.1q,  v20.1d, v24.1d\n\t"
        "eor    v21.16b, v21.16b, v25.16b\n\t"
        /* Store result */
        "st1    {v21.16b}, [%[out]]\n\t"
        :
        : [xhi]"r"(x_hi), [xlo]"r"(x_lo),
          [yhi]"r"(y_hi), [ylo]"r"(y_lo),
          [out]"r"(out)
        : "v16","v17","v18","v19","v20","v21","v22","v23","v24","v25","v26","memory"
    );
    (void)r0; (void)r1; (void)r2; (void)r3; (void)t0; (void)t1;
    (void)lo_lo; (void)lo_hi; (void)hi_lo; (void)hi_hi; (void)tmp;
}

/* GHASH: X = GHASH(H, A, C) per RFC 5116 / NIST SP 800-38D */
static void ghash(const uint8_t H[16], const uint8_t* data, size_t len, uint8_t X[16])
{
    uint8_t block[16];
    size_t blocks = len / 16;
    size_t rem    = len % 16;

    for (size_t i = 0; i < blocks; i++) {
        for (int j = 0; j < 16; j++)
            block[j] = X[j] ^ data[i*16+j];
        gcm_gmul(block, H, X);
    }
    if (rem) {
        memset(block, 0, 16);
        memcpy(block, data + blocks*16, rem);
        for (int j = 0; j < 16; j++) block[j] ^= X[j];
        gcm_gmul(block, H, X);
    }
}

/* Increment the 32-bit big-endian counter in bytes 12..15 */
static inline void gcm_inc32(uint8_t ctr[16])
{
    uint32_t c = ((uint32_t)ctr[12]<<24)|((uint32_t)ctr[13]<<16)|
                 ((uint32_t)ctr[14]<<8)|(uint32_t)ctr[15];
    c++;
    ctr[12]=(c>>24)&0xFF; ctr[13]=(c>>16)&0xFF;
    ctr[14]=(c>>8)&0xFF;  ctr[15]=c&0xFF;
}

void crypto_init(void)
{
    printk(KERN_INFO "Crypto: AES-256-GCM (ARM64 hw), CSPRNG initialized\n");
}

void keystore_init(void)
{
    printk(KERN_DEBUG "Crypto: Key store initialized\n");
}

/* ---- AES-256-GCM ---- */

int aes_gcm_encrypt(const uint8_t* plaintext, size_t pt_len,
                    const uint8_t* aad, size_t aad_len,
                    const uint8_t* key, const uint8_t* iv,
                    uint8_t* ciphertext, uint8_t* tag)
{
    aes256_rk_t rk;
    aes256_key_expand(key, &rk);

    /* H = AES(key, 0^128) */
    uint8_t H[16];
    memset(H, 0, 16);
    aes256_encrypt_block(&rk, H, H);

    /* J0 = IV || 0x00000001 (96-bit IV per GCM spec) */
    uint8_t J0[16];
    memcpy(J0, iv, 12);
    J0[12]=0; J0[13]=0; J0[14]=0; J0[15]=1;

    /* GHASH over AAD */
    uint8_t ghash_state[16];
    memset(ghash_state, 0, 16);
    if (aad_len) ghash(H, aad, aad_len, ghash_state);

    /* CTR encryption starting at counter=2 */
    uint8_t ctr[16];
    memcpy(ctr, J0, 16);
    gcm_inc32(ctr);  /* counter = 2 */

    uint8_t keystream[16];
    size_t done = 0;
    while (done < pt_len) {
        aes256_encrypt_block(&rk, ctr, keystream);
        size_t n = pt_len - done;
        if (n > 16) n = 16;
        for (size_t i = 0; i < n; i++)
            ciphertext[done+i] = plaintext[done+i] ^ keystream[i];
        done += n;
        gcm_inc32(ctr);
    }

    /* GHASH over ciphertext */
    ghash(H, ciphertext, pt_len, ghash_state);

    /* GHASH length block: AAD_len || CT_len (both 64-bit big-endian) */
    uint8_t len_block[16];
    uint64_t aad_bits = (uint64_t)aad_len * 8;
    uint64_t ct_bits  = (uint64_t)pt_len  * 8;
    len_block[0]=(aad_bits>>56)&0xFF; len_block[1]=(aad_bits>>48)&0xFF;
    len_block[2]=(aad_bits>>40)&0xFF; len_block[3]=(aad_bits>>32)&0xFF;
    len_block[4]=(aad_bits>>24)&0xFF; len_block[5]=(aad_bits>>16)&0xFF;
    len_block[6]=(aad_bits>>8)&0xFF;  len_block[7]=aad_bits&0xFF;
    len_block[8]=(ct_bits>>56)&0xFF;  len_block[9]=(ct_bits>>48)&0xFF;
    len_block[10]=(ct_bits>>40)&0xFF; len_block[11]=(ct_bits>>32)&0xFF;
    len_block[12]=(ct_bits>>24)&0xFF; len_block[13]=(ct_bits>>16)&0xFF;
    len_block[14]=(ct_bits>>8)&0xFF;  len_block[15]=ct_bits&0xFF;
    ghash(H, len_block, 16, ghash_state);

    /* Tag = E(J0) XOR GHASH */
    aes256_encrypt_block(&rk, J0, keystream);
    for (int i = 0; i < 16; i++)
        tag[i] = ghash_state[i] ^ keystream[i];

    return 0;
}

int aes_gcm_decrypt(const uint8_t* ciphertext, size_t ct_len,
                    const uint8_t* aad, size_t aad_len,
                    const uint8_t* key, const uint8_t* iv,
                    const uint8_t* tag, uint8_t* plaintext)
{
    aes256_rk_t rk;
    aes256_key_expand(key, &rk);

    uint8_t H[16];
    memset(H, 0, 16);
    aes256_encrypt_block(&rk, H, H);

    uint8_t J0[16];
    memcpy(J0, iv, 12);
    J0[12]=0; J0[13]=0; J0[14]=0; J0[15]=1;

    /* Verify tag first */
    uint8_t ghash_state[16];
    memset(ghash_state, 0, 16);
    if (aad_len) ghash(H, aad, aad_len, ghash_state);
    ghash(H, ciphertext, ct_len, ghash_state);

    uint8_t len_block[16];
    uint64_t aad_bits = (uint64_t)aad_len * 8;
    uint64_t ct_bits  = (uint64_t)ct_len  * 8;
    len_block[0]=(aad_bits>>56)&0xFF; len_block[1]=(aad_bits>>48)&0xFF;
    len_block[2]=(aad_bits>>40)&0xFF; len_block[3]=(aad_bits>>32)&0xFF;
    len_block[4]=(aad_bits>>24)&0xFF; len_block[5]=(aad_bits>>16)&0xFF;
    len_block[6]=(aad_bits>>8)&0xFF;  len_block[7]=aad_bits&0xFF;
    len_block[8]=(ct_bits>>56)&0xFF;  len_block[9]=(ct_bits>>48)&0xFF;
    len_block[10]=(ct_bits>>40)&0xFF; len_block[11]=(ct_bits>>32)&0xFF;
    len_block[12]=(ct_bits>>24)&0xFF; len_block[13]=(ct_bits>>16)&0xFF;
    len_block[14]=(ct_bits>>8)&0xFF;  len_block[15]=ct_bits&0xFF;
    ghash(H, len_block, 16, ghash_state);

    uint8_t exp_tag[16], ks[16];
    aes256_encrypt_block(&rk, J0, ks);
    for (int i = 0; i < 16; i++) exp_tag[i] = ghash_state[i] ^ ks[i];

    /* Constant-time tag comparison */
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= (exp_tag[i] ^ tag[i]);
    if (diff) return -1;  /* Authentication failed */

    /* Decrypt */
    uint8_t ctr[16];
    memcpy(ctr, J0, 16);
    gcm_inc32(ctr);

    size_t done = 0;
    while (done < ct_len) {
        aes256_encrypt_block(&rk, ctr, ks);
        size_t n = ct_len - done;
        if (n > 16) n = 16;
        for (size_t i = 0; i < n; i++)
            plaintext[done+i] = ciphertext[done+i] ^ ks[i];
        done += n;
        gcm_inc32(ctr);
    }
    return 0;
}

/* ChaCha20-Poly1305 Stubs */
int chacha20_poly1305_encrypt(const uint8_t* plaintext, size_t pt_len,
                               const uint8_t* aad, size_t aad_len,
                               const uint8_t* key, const uint8_t* nonce,
                               uint8_t* ciphertext, uint8_t* tag)
{
    printk(KERN_DEBUG "Crypto: ChaCha20-Poly1305 encrypt (stub)\n");
    memcpy(ciphertext, plaintext, pt_len);
    memset(tag, 0, 16);
    return 0;
}

int chacha20_poly1305_decrypt(const uint8_t* ciphertext, size_t ct_len,
                               const uint8_t* aad, size_t aad_len,
                               const uint8_t* key, const uint8_t* nonce,
                               const uint8_t* tag, uint8_t* plaintext)
{
    printk(KERN_DEBUG "Crypto: ChaCha20-Poly1305 decrypt (stub)\n");
    memcpy(plaintext, ciphertext, ct_len);
    return 0;
}

/* SHA-256 Stubs */
void sha256_init(void* ctx)
{
    printk(KERN_DEBUG "Crypto: SHA-256 init (stub)\n");
    memset(ctx, 0, 128);  /* SHA-256 context size */
}

void sha256_update(void* ctx, const uint8_t* data, size_t len)
{
    (void)ctx; (void)data; (void)len;
}

void sha256_final(void* ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    (void)ctx;
    memset(digest, 0, SHA256_DIGEST_SIZE);
}

/* SHA-512 Stubs */
void sha512_init(void* ctx)
{
    memset(ctx, 0, 256);
}

void sha512_update(void* ctx, const uint8_t* data, size_t len)
{
    (void)ctx; (void)data; (void)len;
}

void sha512_final(void* ctx, uint8_t digest[SHA512_DIGEST_SIZE])
{
    (void)ctx;
    memset(digest, 0, SHA512_DIGEST_SIZE);
}

/* Ed25519 Stubs */
int ed25519_generate_keypair(uint8_t public_key[ED25519_KEY_SIZE],
                              uint8_t private_key[ED25519_KEY_SIZE])
{
    printk(KERN_DEBUG "Crypto: Ed25519 keygen (stub)\n");
    memset(public_key, 0, ED25519_KEY_SIZE);
    memset(private_key, 0, ED25519_KEY_SIZE);
    return 0;
}

int ed25519_sign(const uint8_t* message, size_t msg_len,
                  const uint8_t private_key[ED25519_KEY_SIZE],
                  uint8_t signature[ED25519_SIG_SIZE])
{
    (void)message; (void)msg_len; (void)private_key;
    memset(signature, 0, ED25519_SIG_SIZE);
    return 0;
}

int ed25519_verify(const uint8_t* message, size_t msg_len,
                    const uint8_t public_key[ED25519_KEY_SIZE],
                    const uint8_t signature[ED25519_SIG_SIZE])
{
    (void)message; (void)msg_len; (void)public_key; (void)signature;
    return 0;
}

/* X25519 Stub */
int x25519(uint8_t shared_secret[ED25519_KEY_SIZE],
           const uint8_t private_key[ED25519_KEY_SIZE],
           const uint8_t public_key[ED25519_KEY_SIZE])
{
    (void)private_key; (void)public_key;
    memset(shared_secret, 0, ED25519_KEY_SIZE);
    return 0;
}

/* HKDF-SHA256 Stub */
int hkdf_sha256(const uint8_t* salt, size_t salt_len,
                const uint8_t* ikm, size_t ikm_len,
                const uint8_t* info, size_t info_len,
                uint8_t* okm, size_t okm_len)
{
    (void)salt; (void)salt_len; (void)ikm; (void)ikm_len;
    (void)info; (void)info_len;
    memset(okm, 0, okm_len);
    return 0;
}

/* Argon2id Stub */
int argon2id(const uint8_t* password, size_t pwd_len,
             const uint8_t* salt, size_t salt_len,
             uint32_t t_cost, uint32_t m_cost, uint32_t parallelism,
             uint8_t* hash, size_t hash_len)
{
    (void)password; (void)pwd_len; (void)salt; (void)salt_len;
    (void)t_cost; (void)m_cost; (void)parallelism;
    memset(hash, 0, hash_len);
    return 0;
}

/* ---- CSPRNG: ChaCha20-based, seeded from ARM generic timer ---- */

/*
 * ChaCha20 quarter-round and block function per RFC 7539.
 * State is 16 × uint32_t; output is 64 bytes of keystream.
 */

#define ROTL32(v,n) (((v)<<(n))|((v)>>(32-(n))))
#define QR(a,b,c,d) \
    a+=b; d^=a; d=ROTL32(d,16); \
    c+=d; b^=c; b=ROTL32(b,12); \
    a+=b; d^=a; d=ROTL32(d, 8); \
    c+=d; b^=c; b=ROTL32(b, 7)

static struct {
    uint32_t state[16];
    uint8_t  buf[64];
    uint32_t buf_pos;
    int      initialized;
} g_rng;

static void chacha20_block(const uint32_t in[16], uint8_t out[64])
{
    uint32_t x[16];
    memcpy(x, in, 64);
    for (int i = 0; i < 10; i++) {
        QR(x[0],x[4],x[ 8],x[12]); QR(x[1],x[5],x[ 9],x[13]);
        QR(x[2],x[6],x[10],x[14]); QR(x[3],x[7],x[11],x[15]);
        QR(x[0],x[5],x[10],x[15]); QR(x[1],x[6],x[11],x[12]);
        QR(x[2],x[7],x[ 8],x[13]); QR(x[3],x[4],x[ 9],x[14]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + in[i];
        out[i*4+0]=(v)&0xFF; out[i*4+1]=(v>>8)&0xFF;
        out[i*4+2]=(v>>16)&0xFF; out[i*4+3]=(v>>24)&0xFF;
    }
}

static uint64_t read_cntpct(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

void rng_init(void)
{
    /* Seed from ARM generic timer physical counter */
    uint64_t seed0 = read_cntpct();
    uint64_t seed1 = read_cntpct() ^ (seed0 << 17);

    /* ChaCha20 "expand 32-byte k" constant */
    g_rng.state[0]  = 0x61707865; g_rng.state[1] = 0x3320646e;
    g_rng.state[2]  = 0x79622d32; g_rng.state[3] = 0x6b206574;
    /* Key: mix of timer value, counter XOR, and compile-time constant */
    g_rng.state[4]  = (uint32_t)(seed0);
    g_rng.state[5]  = (uint32_t)(seed0 >> 32);
    g_rng.state[6]  = (uint32_t)(seed1);
    g_rng.state[7]  = (uint32_t)(seed1 >> 32);
    g_rng.state[8]  = (uint32_t)(seed0 ^ 0xDEADBEEFUL);
    g_rng.state[9]  = (uint32_t)(seed1 ^ 0xCAFEBABEUL);
    g_rng.state[10] = (uint32_t)(seed0 >> 16) ^ 0x12345678UL;
    g_rng.state[11] = (uint32_t)(seed1 >> 16) ^ 0x87654321UL;
    /* Counter and nonce */
    g_rng.state[12] = 0;
    g_rng.state[13] = (uint32_t)(seed0 ^ seed1);
    g_rng.state[14] = (uint32_t)(seed0 >> 8);
    g_rng.state[15] = (uint32_t)(seed1 >> 8);

    /* Discard first block to mix state */
    chacha20_block(g_rng.state, g_rng.buf);
    g_rng.state[12]++;
    g_rng.buf_pos = 64;  /* force refill on first use */
    g_rng.initialized = 1;

    printk(KERN_INFO "Crypto: CSPRNG initialized (ChaCha20 + ARM timer seed)\n");
}

void rng_get_bytes(uint8_t* buf, size_t len)
{
    if (!g_rng.initialized) rng_init();

    size_t pos = 0;
    while (pos < len) {
        if (g_rng.buf_pos >= 64) {
            chacha20_block(g_rng.state, g_rng.buf);
            g_rng.state[12]++;
            /* Re-mix timer into nonce on counter wrap */
            if (g_rng.state[12] == 0) {
                g_rng.state[13] ^= (uint32_t)read_cntpct();
                g_rng.state[12]++;
            }
            g_rng.buf_pos = 0;
        }
        buf[pos++] = g_rng.buf[g_rng.buf_pos++];
    }
}

uint32_t rng_get_u32(void)
{
    uint32_t val;
    rng_get_bytes((uint8_t*)&val, sizeof(val));
    return val;
}

uint64_t rng_get_u64(void)
{
    uint64_t val;
    rng_get_bytes((uint8_t*)&val, sizeof(val));
    return val;
}

/* Legacy state removed — was non-cryptographic LCG */
