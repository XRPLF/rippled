
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

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

// Our ciphertext is all encrypted except the IV. The encrypted data decodes as follows:
// 1) 256-bits of SHA-512 HMAC of original plaintext
// 2) Original plaintext

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

std::vector<unsigned char> CKey::encryptECIES(CKey& otherKey, const std::vector<unsigned char>& plaintext)
{
	std::vector<unsigned char> secret=getECIESSecret(otherKey);

	uint256 hmac=makeHMAC(secret, plaintext);

	uint128 iv;
	if(RAND_bytes(static_cast<unsigned char *>(iv.begin()), 128/8) != 1)
		throw std::runtime_error("insufficient entropy");

	EVP_CIPHER_CTX ctx;
	EVP_CIPHER_CTX_init(&ctx);

	if (EVP_EncryptInit_ex(&ctx, EVP_aes_128_cbc(), NULL,
		&(secret.front()), static_cast<unsigned char *>(iv.begin())) != 1)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("init cipher ctx");
	}

	std::vector<unsigned char> out(plaintext.size() + (256/8) + (512/8) + 48, 0);
	int len=0, bytesWritten;

	// output 256-bit IV
	memcpy(&(out.front()), iv.begin(), 32);
	len=32;

	// Encrypt/output 512-bit HMAC
	bytesWritten=out.capacity()-len;
	assert(bytesWritten>0);
	if(EVP_EncryptUpdate(&ctx, &(out.front())+len, &bytesWritten, hmac.begin(), 64) < 0)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("");
	}
	len+=bytesWritten;

	// encrypt/output plaintext
	bytesWritten=out.capacity()-len;
	assert(bytesWritten>0);
	if(EVP_EncryptUpdate(&ctx, &(out.front())+len, &bytesWritten, &(plaintext.front()), plaintext.size()) < 0)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("");
	}
	len+=bytesWritten;

	// finalize
	bytesWritten=out.capacity()-len;
	if(EVP_EncryptFinal_ex(&ctx, &(out.front())+len, &bytesWritten) < 0)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("");
	}
	len+=bytesWritten;

	out.resize(len);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return out;
}

std::vector<unsigned char> CKey::decryptECIES(CKey& otherKey, const std::vector<unsigned char>& ciphertext)
{
	std::vector<unsigned char> secret=getECIESSecret(otherKey);

	// 1) Decrypt

	// 2) Extract length and plaintext
	
	// 3) Compute HMAC
	
	// 4) Verify

}

// vim:ts=4
