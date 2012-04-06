
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <vector>
#include <cassert>

#include "key.h"

#define ECIES_KEY_HASH		SHA256
#define ECIES_KEY_LENGTH	(256/8)
#define ECIES_ENC_ALGO		EVP_aes_256_cbc()
#define ECIES_ENC_SIZE 		(256/8)
#define ECIES_ENC_TYPE		uint256
#define ECIES_HMAC_ALGO		EVP_sha256()
#define ECIES_HMAC_SIZE		(256/8)
#define ECIES_HMAC_TYPE		uint256

static void* ecies_key_derivation(const void *input, size_t ilen, void *output, size_t *olen)
{ // This function must not be changed as it must be what ECDH_compute_key expects
	if (*olen < ECIES_KEY_LENGTH)
		return NULL;
	*olen = ECIES_KEY_LENGTH;
	return ECIES_KEY_HASH(static_cast<const unsigned char *>(input), ilen, static_cast<unsigned char *>(output));
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

	std::vector<unsigned char> ret(ECIES_KEY_LENGTH);
	if (ECDH_compute_key(&(ret.front()), ECIES_KEY_LENGTH, EC_KEY_get0_public_key(pubkey),
			privkey, ecies_key_derivation) != ECIES_KEY_LENGTH)
		throw std::runtime_error("ecdh key failed");
	return ret;
}

// Our ciphertext is all encrypted except the IV. The encrypted data decodes as follows:
// 1) 256-bit IV (unencrypted)
// 2) Encrypted: 256-bits HMAC of original plaintext
// 3) Encrypted: Original plaintext
// 4) Encrypted: Rest of block/padding

static ECIES_HMAC_TYPE makeHMAC(const std::vector<unsigned char>& secret, const std::vector<unsigned char> data)
{
	HMAC_CTX ctx;
	HMAC_CTX_init(&ctx);
	
	if(HMAC_Init_ex(&ctx, &(secret.front()), secret.size(), ECIES_HMAC_ALGO, NULL) != 1)
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

	ECIES_HMAC_TYPE ret;
	memcpy(ret.begin(), &(hmac.front()), ECIES_HMAC_SIZE);

	return ret;
}

std::vector<unsigned char> CKey::encryptECIES(CKey& otherKey, const std::vector<unsigned char>& plaintext)
{
	std::vector<unsigned char> secret=getECIESSecret(otherKey);

	ECIES_HMAC_TYPE hmac=makeHMAC(secret, plaintext);

	uint128 iv;
	if(RAND_bytes(static_cast<unsigned char *>(iv.begin()), ECIES_ENC_SIZE) != 1)
		throw std::runtime_error("insufficient entropy");

	EVP_CIPHER_CTX ctx;
	EVP_CIPHER_CTX_init(&ctx);

	if (EVP_EncryptInit_ex(&ctx, ECIES_ENC_ALGO, NULL,
		&(secret.front()), static_cast<unsigned char *>(iv.begin())) != 1)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("init cipher ctx");
	}

	std::vector<unsigned char> out(plaintext.size() + ECIES_HMAC_SIZE + (ECIES_ENC_SIZE*2), 0);
	int len=0, bytesWritten;

	// output IV
	memcpy(&(out.front()), iv.begin(), ECIES_ENC_SIZE);
	len=ECIES_ENC_SIZE;

	// Encrypt/output HMAC
	bytesWritten=out.capacity()-len;
	assert(bytesWritten>0);
	if(EVP_EncryptUpdate(&ctx, &(out.front())+len, &bytesWritten, hmac.begin(), ECIES_HMAC_SIZE) < 0)
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

	// minimum ciphertext = IV + HMAC + 1 block
	if(ciphertext.size() < ((2*ECIES_ENC_SIZE)+ECIES_HMAC_SIZE) )
		throw std::runtime_error("ciphertext too short");

	// extract IV
	ECIES_ENC_TYPE iv;
	memcpy(iv.begin(), &(ciphertext.front()), ECIES_ENC_SIZE);

	// begin decrypting
	EVP_CIPHER_CTX ctx;
	EVP_CIPHER_CTX_init(&ctx);

	if(EVP_DecryptInit_ex(&ctx, ECIES_ENC_ALGO, NULL, &(secret.front()), iv.begin()) != 1)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("unable to init cipher");
	}
	
	// decrypt mac
	ECIES_HMAC_TYPE hmac;
	int outlen=ECIES_HMAC_SIZE;
	if( (EVP_DecryptUpdate(&ctx, hmac.begin(), &outlen, &(ciphertext.front()), ECIES_HMAC_SIZE) != 1) ||
		(outlen != ECIES_HMAC_SIZE) )
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("unable to extract hmac");
	}
	
	// decrypt plaintext
	std::vector<unsigned char> plaintext(ciphertext.size() - ECIES_HMAC_SIZE - ECIES_ENC_SIZE);
	outlen=plaintext.size();
	if(EVP_DecryptUpdate(&ctx, &(plaintext.front()), &outlen,
		&(ciphertext.front())+ECIES_HMAC_SIZE, ciphertext.size()-ECIES_HMAC_SIZE) != 1)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("unable to extract plaintext");
	}

	int flen=0;
	if(EVP_DecryptFinal(&ctx, &(plaintext.front()) + outlen, &flen) != 1)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("plaintext had bad padding");
	}
	plaintext.resize(flen+outlen);

	if(hmac != makeHMAC(secret, plaintext))
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw std::runtime_error("plaintext had bad hmac");
	}

	EVP_CIPHER_CTX_cleanup(&ctx);
	return plaintext;
}

// vim:ts=4
