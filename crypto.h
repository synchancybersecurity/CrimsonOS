#ifndef _CRIMSON_CRYPTO_H
#define _CRIMSON_CRYPTO_H

#include <crimson/types.h>

#define AES_KEY_SIZE        32
#define AES_BLOCK_SIZE      16
#define AES_IV_SIZE         16
#define AES_GCM_TAG_SIZE    16
#define CHACHA_KEY_SIZE     32
#define CHACHA_NONCE_SIZE   12
#define CHACHA_BLOCK_SIZE   64
#define ED25519_KEY_SIZE    32
#define ED25519_SIG_SIZE    64
#define SHA256_DIGEST_SIZE  32
#define SHA512_DIGEST_SIZE  64

void crypto_init(void);
void keystore_init(void);

int aes_gcm_encrypt(const uint8_t* pt, size_t pt_len, const uint8_t* aad, size_t aad_len,
                    const uint8_t* key, const uint8_t* iv, uint8_t* ct, uint8_t* tag);
int aes_gcm_decrypt(const uint8_t* ct, size_t ct_len, const uint8_t* aad, size_t aad_len,
                    const uint8_t* key, const uint8_t* iv, const uint8_t* tag, uint8_t* pt);

int chacha20_poly1305_encrypt(const uint8_t* pt, size_t pt_len, const uint8_t* aad, size_t aad_len,
                               const uint8_t* key, const uint8_t* nonce, uint8_t* ct, uint8_t* tag);
int chacha20_poly1305_decrypt(const uint8_t* ct, size_t ct_len, const uint8_t* aad, size_t aad_len,
                               const uint8_t* key, const uint8_t* nonce, const uint8_t* tag, uint8_t* pt);

void sha256_init(void* ctx);
void sha256_update(void* ctx, const uint8_t* data, size_t len);
void sha256_final(void* ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

void sha512_init(void* ctx);
void sha512_update(void* ctx, const uint8_t* data, size_t len);
void sha512_final(void* ctx, uint8_t digest[SHA512_DIGEST_SIZE]);

int ed25519_generate_keypair(uint8_t pk[ED25519_KEY_SIZE], uint8_t sk[ED25519_KEY_SIZE]);
int ed25519_sign(const uint8_t* msg, size_t len, const uint8_t sk[ED25519_KEY_SIZE], uint8_t sig[ED25519_SIG_SIZE]);
int ed25519_verify(const uint8_t* msg, size_t len, const uint8_t pk[ED25519_KEY_SIZE], const uint8_t sig[ED25519_SIG_SIZE]);

int x25519(uint8_t ss[ED25519_KEY_SIZE], const uint8_t sk[ED25519_KEY_SIZE], const uint8_t pk[ED25519_KEY_SIZE]);
int hkdf_sha256(const uint8_t* salt, size_t salt_len, const uint8_t* ikm, size_t ikm_len,
                const uint8_t* info, size_t info_len, uint8_t* okm, size_t okm_len);
int argon2id(const uint8_t* pwd, size_t pwd_len, const uint8_t* salt, size_t salt_len,
             uint32_t t_cost, uint32_t m_cost, uint32_t parallelism, uint8_t* hash, size_t hash_len);

void rng_init(void);
void rng_get_bytes(uint8_t* buf, size_t len);
uint32_t rng_get_u32(void);
uint64_t rng_get_u64(void);

#endif
