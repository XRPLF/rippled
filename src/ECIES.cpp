
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>

#include <vector>
#include <cassert>

#include "key.h"

static void* ecies_key_derivation(const void *input, size_t ilen, void *output, size_t *olen)
{ // This function must not be changed as it must be what ECDH_compute_key expects
	if (*olen < SHA512_DIGEST_LENGTH)
		return NULL;
	*olen = SHA512_DIGEST_LENGTH;
	return SHA512(static_cast<const unsigned char *>(input), ilen, static_cast<unsigned char *>(output));
}

std::vector<unsigned char> CKey::getECIESSecret(CKey& otherKey)
{ // Retrieve a secret generated from an EC key pair. At least one private key must be known.
	if(!pkey || !otherKey.pkey)
		throw std::runtime_error("missing key");

	EC_KEY *pubkey, *privkey;
	if(EC_KEY_get0_private_key(pkey))
	{
		privkey=pkey;
		pubkey=otherKey.pkey;
	}
	else if(EC_KEY_get0_private_key(otherKey.pkey))
	{
		privkey=otherKey.pkey;
		pubkey=pkey;
	}
	else throw std::runtime_error("no private key");

	std::vector<unsigned char> ret(SHA512_DIGEST_LENGTH);
	if (ECDH_compute_key(&(ret.front()), SHA512_DIGEST_LENGTH, EC_KEY_get0_public_key(pubkey),
			privkey, ecies_key_derivation) != SHA512_DIGEST_LENGTH)
		throw std::runtime_error("ecdh key failed");
	return ret;
}

// Our ciphertext is all encrypted. The encrypted data decodes as follows:
// 1) 256-bits of SHA-512 HMAC of original plaintext
// 2) 128-bit IV
// 3) Original plaintext

static uint256 makeHMAC(const std::vector<unsigned char>& secret, const std::vector<unsigned char> data)
{
	HMAC_CTX ctx;
	HMAC_CTX_init(&ctx);
	
	if(HMAC_Init_ex(&ctx, &(secret.front()), secret.size(), EVP_sha512(), NULL) != 1)
	{
		HMAC_CTX_cleanup(&ctx);
		throw std::runtime_error("init hmac");
	}

	if(HMAC_Update(&ctx, &(data.front()), data.size()) != 1)
	{
		HMAC_CTX_cleanup(&ctx);
		throw std::runtime_error("update hmac");
	}

	unsigned int ml=EVP_MAX_MD_SIZE;
	std::vector<unsigned char> hmac(ml);
	if(!HMAC_Final(&ctx, &(hmac.front()), &ml) != 1)
	{
		HMAC_CTX_cleanup(&ctx);
		throw std::runtime_error("finalize hmac");
	}
	assert((ml>=32) && (ml<=EVP_MAX_MD_SIZE));

	uint256 ret;
	memcpy(ret.begin(), &(hmac.front()), 32);

	return ret;
}

#if 0

std::vector<unsigned char> CKey::encryptECIES(CKey& otherKey, const std::vector<unsigned char>& plaintext)
{
	std::vector<unsigned char> secret=getECIESSecret(otherKey);

	uint256 hmac=makeHMAC(secret, plaintext);

	uint128 iv;
	if(RAND_bytes((unsigned char *) iv.begin(), 128/8) != 1)
		throw std::runtime_error("insufficient entropy");

	ECP_CIPHER_CTX ctx;
	EVP_CIPHER_CTX_init(&ctx);

	if (EVP_EncryptInit_ex(&ctx, EVP_AES_128_cbc(), NULL, key, iv) != 1)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("init cipher ctx");
	}

	EVP_EncryptUpdate
	EVP_EncryptUpdate
	EVP_EncryptUpdate
	
	ECP_EncryptFinal_ex
}

std::vector<unsigned char> CKey::decryptECIES(CKey& otherKey, const std::Vector<unsigned char>& ciphertext)
{
	std::vector<unsigned char> secret=getECIESSecret(otherKey);

	// 1) Decrypt

	// 2) Extract length and plaintext
	
	// 3) Compute HMAC
	
	// 4) Verify

}

#endif

// vim:ts=4
