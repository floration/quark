// Update-package signatures: Ed25519ph (RFC 8032, SHA-512 prehash) over a whole file, via vendored Monocypher.
// Keys and signatures are lowercase hex text: private key = 32-byte seed (64 hex), public key 64 hex,
// signature 128 hex. A .sig file holds the signature hex (surrounding whitespace ignored).
#pragma once
#include <string>

namespace sign {

// The release key's public half: the launcher and server autoupdate refuse packages not signed by it.
extern const char* kReleasePublicKey;

bool keygen(std::string* privHex, std::string* pubHex);                     // fresh keypair from the OS RNG
bool publicFromPrivate(const std::string& privHex, std::string* pubHex);
bool signData(const std::string& data, const std::string& privHex, std::string* sigHex);
bool signFile(const std::string& path, const std::string& privHex, std::string* sigHex, std::string* err);
// True only when sigHex is a valid signature of the data / file by pubHex.
bool verifyData(const std::string& data, const std::string& sigHex, const std::string& pubHex);
bool verifyFile(const std::string& path, const std::string& sigHex, const std::string& pubHex, std::string* err);

// Plain Ed25519 (RFC 8032, no prehash) over a short message: ranked ids prove their key with it (ranked.h), and the
// Worker checks the same signatures with WebCrypto's Ed25519.
bool signMessage(const std::string& msg, const std::string& privHex, std::string* sigHex);
bool verifyMessage(const std::string& msg, const std::string& sigHex, const std::string& pubHex);

} // namespace sign
