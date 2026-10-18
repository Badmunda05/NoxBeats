// MTProto 2.0 cryptography (OpenSSL): SHA-1/256, AES-256-IGE, key derivation.
#pragma once
#include <cstdint>
#include <vector>

namespace bot::mtp::crypto {
using Bytes = std::vector<std::uint8_t>;

Bytes sha1(const std::uint8_t* d, std::size_t n);
Bytes sha256(const std::uint8_t* d, std::size_t n);
void random_bytes(std::uint8_t* out, std::size_t n);
std::uint64_t random_u64();

// 8-byte auth_key_id = low 64 bits of SHA1(auth_key)
Bytes auth_key_id(const Bytes& auth_key);

// MTProto 2.0 encryption. `from_client` selects the x offset (0 client->server, 8 server->client).
// `plain` must already be padded to a multiple of 16. Returns auth_key_id(8) + msg_key(16) + ciphertext.
Bytes encrypt_packet(const Bytes& auth_key, const Bytes& key_id, const Bytes& plain, bool from_client);
// Returns the plaintext (with padding) or an empty vector if msg_key verification fails.
Bytes decrypt_packet(const Bytes& auth_key, const std::uint8_t* pkt, std::size_t n, bool from_client);

void ige_encrypt(const std::uint8_t* in, std::uint8_t* out, std::size_t n, const std::uint8_t key[32], const std::uint8_t iv[32]);
void ige_decrypt(const std::uint8_t* in, std::uint8_t* out, std::size_t n, const std::uint8_t key[32], const std::uint8_t iv[32]);
}  // namespace bot::mtp::crypto
