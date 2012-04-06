// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KEY_H
#define BITCOIN_KEY_H

#include <stdexcept>
#include <vector>
#include <cassert>

#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

#include <boost/shared_ptr.hpp>

#include "SecureAllocator.h"
#include "NewcoinAddress.h"
#include "uint256.h"
#include "base58.h"

// secp256k1:
// const unsigned int PRIVATE_KEY_SIZE = 279;
// const unsigned int PUBLIC_KEY_SIZE  = 65; // but we don't use full keys
// const unsigned int COMPUB_KEY_SIZE  = 33;
// const unsigned int SIGNATURE_SIZE   = 72;
//
// see www.keylength.com
// script supports up to 75 for single byte push

int static inline EC_KEY_regenerate_key(EC_KEY *eckey, BIGNUM *priv_key)
{
	int ok = 0;
	BN_CTX *ctx = NULL;
	EC_POINT *pub_key = NULL;

	if (!eckey) return 0;

	const EC_GROUP *group = EC_KEY_get0_group(eckey);

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	pub_key = EC_POINT_new(group);

	if (pub_key == NULL)
		goto err;

	if (!EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, ctx))
		goto err;

	EC_KEY_set_conv_form(eckey, POINT_CONVERSION_COMPRESSED);
	EC_KEY_set_private_key(eckey, priv_key);
	EC_KEY_set_public_key(eckey, pub_key);

	ok = 1;

err:

	if (pub_key)
		EC_POINT_free(pub_key);
	if (ctx != NULL)
		BN_CTX_free(ctx);

	return(ok);
}


class key_error : public std::runtime_error
{
public:
	explicit key_error(const std::string& str) : std::runtime_error(str) {}
};



//JED: typedef std::vector<unsigned char, secure_allocator<unsigned char> > CPrivKey;
//typedef std::vector<unsigned char, secure_allocator<unsigned char> > CSecret;

typedef std::vector<unsigned char > CPrivKey;
typedef std::vector<unsigned char > CSecret;
class CKey
{
protected:
	EC_KEY* pkey;
	bool fSet;


public:
	typedef boost::shared_ptr<CKey> pointer;

	CKey()
	{
		pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
		EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);
		if (pkey == NULL)
			throw key_error("CKey::CKey() : EC_KEY_new_by_curve_name failed");
		fSet = false;
	}

	CKey(const CKey& b)
	{
		pkey = EC_KEY_dup(b.pkey);
		EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);
		if (pkey == NULL)
			throw key_error("CKey::CKey(const CKey&) : EC_KEY_dup failed");
		fSet = b.fSet;
	}

	CKey& operator=(const CKey& b)
	{
		if (!EC_KEY_copy(pkey, b.pkey))
			throw key_error("CKey::operator=(const CKey&) : EC_KEY_copy failed");
		fSet = b.fSet;
		return (*this);
	}


	~CKey()
	{
		EC_KEY_free(pkey);
	}


	static uint128 PassPhraseToKey(const std::string& passPhrase);
	static EC_KEY* GenerateRootDeterministicKey(const uint128& passPhrase);
	static EC_KEY* GenerateRootPubKey(BIGNUM* pubGenerator);
	static EC_KEY* GeneratePublicDeterministicKey(const NewcoinAddress& generator, int n);
	static EC_KEY* GeneratePrivateDeterministicKey(const NewcoinAddress& family, const BIGNUM* rootPriv, int n);

	CKey(const uint128& passPhrase) : fSet(true)
	{
		pkey = GenerateRootDeterministicKey(passPhrase);
		assert(pkey);
	}

	CKey(const NewcoinAddress& generator, int n) : fSet(true)
	{ // public deterministic key
		pkey = GeneratePublicDeterministicKey(generator, n);
		assert(pkey);
	}

	CKey(const NewcoinAddress& base, const BIGNUM* rootPrivKey, int n) : fSet(true)
	{ // private deterministic key
		pkey = GeneratePrivateDeterministicKey(base, rootPrivKey, n);
		assert(pkey);
	}

	bool IsNull() const
	{
		return !fSet;
	}

	void MakeNewKey()
	{
		if (!EC_KEY_generate_key(pkey))
			throw key_error("CKey::MakeNewKey() : EC_KEY_generate_key failed");
		EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);
		fSet = true;
	}

	bool SetPrivKey(const CPrivKey& vchPrivKey)
	{
		const unsigned char* pbegin = &vchPrivKey[0];
		if (!d2i_ECPrivateKey(&pkey, &pbegin, vchPrivKey.size()))
			return false;
		EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);
		fSet = true;
		return true;
	}

	bool SetSecret(const CSecret& vchSecret)
	{
		EC_KEY_free(pkey);
		pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
		if (pkey == NULL)
			throw key_error("CKey::SetSecret() : EC_KEY_new_by_curve_name failed");
		if (vchSecret.size() != 32)
			throw key_error("CKey::SetSecret() : secret must be 32 bytes");
		BIGNUM *bn = BN_bin2bn(&vchSecret[0], 32, BN_new());
		if (bn == NULL) 
			throw key_error("CKey::SetSecret() : BN_bin2bn failed");
		if (!EC_KEY_regenerate_key(pkey, bn))
			throw key_error("CKey::SetSecret() : EC_KEY_regenerate_key failed");
		BN_clear_free(bn);
		fSet = true;
		return true;
	}

	CSecret GetSecret() const
	{
		CSecret vchRet;
		vchRet.resize(32);
		const BIGNUM *bn = EC_KEY_get0_private_key(pkey);
		int nBytes = BN_num_bytes(bn);
		if (bn == NULL)
			throw key_error("CKey::GetSecret() : EC_KEY_get0_private_key failed");
		int n=BN_bn2bin(bn, &vchRet[32 - nBytes]);
		if (n != nBytes) 
			throw key_error("CKey::GetSecret(): BN_bn2bin failed");
		return vchRet;
	}

	BIGNUM* GetSecretBN() const
	{
		return BN_dup(EC_KEY_get0_private_key(pkey));
	}

	CPrivKey GetPrivKey() const
	{
		unsigned int nSize = i2d_ECPrivateKey(pkey, NULL);
		if (!nSize)
			throw key_error("CKey::GetPrivKey() : i2d_ECPrivateKey failed");
		assert(nSize<=279);
		CPrivKey vchPrivKey(279, 0);
		unsigned char* pbegin = &vchPrivKey[0];
		if (i2d_ECPrivateKey(pkey, &pbegin) != nSize)
			throw key_error("CKey::GetPrivKey() : i2d_ECPrivateKey returned unexpected size");
		assert(vchPrivKey.size()<=279);
		while(vchPrivKey.size()<279) vchPrivKey.push_back((unsigned char)0);
		return vchPrivKey;
	}

	bool SetPubKey(const std::vector<unsigned char>& vchPubKey)
	{
		const unsigned char* pbegin = &vchPubKey[0];
		if (!o2i_ECPublicKey(&pkey, &pbegin, vchPubKey.size()))
			return false;
		EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);
		fSet = true;
		return true;
	}

	std::vector<unsigned char> GetPubKey() const
	{
		unsigned int nSize = i2o_ECPublicKey(pkey, NULL);
		assert(nSize<=33);
		if (!nSize)
			throw key_error("CKey::GetPubKey() : i2o_ECPublicKey failed");
		std::vector<unsigned char> vchPubKey(33, 0);
		unsigned char* pbegin = &vchPubKey[0];
		if (i2o_ECPublicKey(pkey, &pbegin) != nSize)
			throw key_error("CKey::GetPubKey() : i2o_ECPublicKey returned unexpected size");
		assert(vchPubKey.size()<=33);
		while(vchPubKey.size()<33) vchPubKey.push_back((unsigned char)0);
		return vchPubKey;
	}

	bool Sign(const uint256& hash, std::vector<unsigned char>& vchSig)
	{
		vchSig.clear();
		unsigned char pchSig[10000];
		unsigned int nSize = 0;
		if (!ECDSA_sign(0, (unsigned char*)&hash, sizeof(hash), pchSig, &nSize, pkey))
			return false;

		while(nSize<72)
		{ // enlarge to 72 bytes
			pchSig[nSize]=0;
			nSize++;
		}
		assert(nSize==72);
		vchSig.resize(nSize);
		memcpy(&vchSig[0], pchSig, nSize);
		return true;
	}

	bool Verify(const uint256& hash, const std::vector<unsigned char>& vchSig) const
	{
		// -1 = error, 0 = bad sig, 1 = good
		if (ECDSA_verify(0, (unsigned char*)&hash, sizeof(hash), &vchSig[0], vchSig.size(), pkey) != 1)
			return false;
		return true;
	}

	// ECIES functions. These throw on failure

	// returns a 32-byte secret unique to these two keys. At least one private key must be known.
	uint256 getECIESSecret(CKey& otherKey);

	// encrypt/decrypt functions with integrity checking.
	// Note that the other side must somehow know what keys to use
	std::vector<unsigned char> encryptECIES(CKey& otherKey, const std::vector<unsigned char>& plaintext);
	std::vector<unsigned char> decryptECIES(CKey& otherKey, const std::vector<unsigned char>& ciphertext);
};

#endif
// vim:ts=4
