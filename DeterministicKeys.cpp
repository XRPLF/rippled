#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

// Functions to add CKey support for deterministic EC keys

#include "Serializer.h"

uint256 CKey::PassPhraseToKey(const std::string& passPhrase)
{
	Serializer s;
	s.addRaw(passPhrase.c_str(), passPhrase.size());
	uint256 ret(s.getSHA512Half());
	s.secureErase();
	return ret;
}

EC_KEY* CKey::GenerateRootDeterministicKey(const uint256& key)
{
	BN_CTX* ctx=BN_CTX_new();
	if(!ctx) return NULL;

	EC_KEY* pkey=EC_KEY_new_by_curve_name(NID_secp256k1);
	if(!pkey)
	{
		BN_CTX_free(ctx);
		return NULL;
	}
	EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);

	BIGNUM* order=BN_new();
	if(!order)
	{
		BN_CTX_free(ctx);
		EC_KEY_free(pkey);
		return NULL;
	}
	if(!EC_GROUP_get_order(EC_KEY_get0_group(pkey), order, ctx))
	{
		assert(false);
		BN_free(order);
		EC_KEY_free(pkey);
		BN_CTX_free(ctx);
		return NULL;
	}

	BIGNUM *privKey=NULL;
	int seq=0;
	do
	{ // private key must be non-zero and less than the curve's order
		Serializer s(72);
		s.add256(key);
		s.add32(seq++);
		uint256 root=s.getSHA512Half();
		s.secureErase();
		privKey=BN_bin2bn((const unsigned char *) &root, sizeof(root), privKey);
		if(privKey==NULL)
		{
			EC_KEY_free(pkey);
			BN_free(order);
			BN_CTX_free(ctx);
		}
		root.zero();
	} while(BN_is_zero(privKey) || (BN_cmp(privKey, order)>=0));

	BN_free(order);

	if(!EC_KEY_set_private_key(pkey, privKey))
	{ // set the random point as the private key
		assert(false);
		EC_KEY_free(pkey);
		BN_free(privKey);
		BN_CTX_free(ctx);
		return NULL;
	}

	EC_POINT *pubKey=EC_POINT_new(EC_KEY_get0_group(pkey));
	if(!EC_POINT_mul(EC_KEY_get0_group(pkey), pubKey, privKey, NULL, NULL, ctx))
	{ // compute the corresponding public key point
		assert(false);
		BN_free(privKey);
		EC_POINT_free(pubKey);
		EC_KEY_free(pkey);
		BN_CTX_free(ctx);
		return NULL;
	}
	BN_free(privKey);
	if(!EC_KEY_set_public_key(pkey, pubKey))
	{
		assert(false);
		EC_POINT_free(pubKey);
		EC_KEY_free(pkey);
		BN_CTX_free(ctx);
		return NULL;
	}
	EC_POINT_free(pubKey);

	BN_CTX_free(ctx);

	assert(EC_KEY_check_key(pkey)==1);
	return pkey;
}

static BIGNUM* makeHash(const uint160& family, int seq)
{
	Serializer s;
	s.add160(family);
	s.add32(seq);
	uint256 root=s.getSHA512Half();
	s.secureErase();
	return BN_bin2bn((const unsigned char *) &root, sizeof(root), NULL);
}

EC_KEY* CKey::GeneratePublicDeterministicKey(const uint160& family, EC_POINT* rootPubKey, int seq)
{ // publicKey(n) = rootPublicKey EC_POINT_+ Hash(pubHash|seq)*point
	BN_CTX* ctx=BN_CTX_new();
	if(ctx==NULL) return NULL;

	EC_KEY* pkey=EC_KEY_new_by_curve_name(NID_secp256k1);
	if(pkey==NULL)
	{
		BN_CTX_free(ctx);
		return NULL;
	}
	EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);

	EC_POINT *newPoint=EC_POINT_new(EC_KEY_get0_group(pkey));
	if(newPoint==NULL)
	{
		EC_KEY_free(pkey);
		BN_CTX_free(ctx);
		return NULL;
	}
	
	BIGNUM* hash=makeHash(family, seq);
	if(hash==NULL)
	{
		EC_POINT_free(newPoint);
		BN_CTX_free(ctx);
		EC_KEY_free(pkey);
		return NULL;
	}
	
	EC_POINT_mul(EC_KEY_get0_group(pkey), newPoint, hash, NULL, NULL, ctx);
	BN_free(hash);

	EC_POINT_add(EC_KEY_get0_group(pkey), newPoint, newPoint, rootPubKey, ctx);
	EC_KEY_set_public_key(pkey, newPoint);

	EC_POINT_free(newPoint);
	BN_CTX_free(ctx);

	return pkey;
}

EC_KEY* CKey::GeneratePrivateDeterministicKey(const uint160& family, BIGNUM* rootPrivKey, int seq)
{ // privateKey(n) = (rootPrivateKey + Hash(pubHash|seq)) % order
	BN_CTX* ctx=BN_CTX_new();
	if(ctx==NULL) return NULL;

	EC_KEY* pkey=EC_KEY_new_by_curve_name(NID_secp256k1);
	if(pkey==NULL)
	{
		BN_CTX_free(ctx);
		return NULL;
	}
	EC_KEY_set_conv_form(pkey, POINT_CONVERSION_COMPRESSED);
	
	BIGNUM* order=BN_new();
	if(order==NULL)
	{
		BN_CTX_free(ctx);
		EC_KEY_free(pkey);
	}
	
	EC_GROUP_get_order(EC_KEY_get0_group(pkey), order, ctx);
	
	BIGNUM* privKey=makeHash(family, seq);
	BN_mod_add(privKey, privKey, rootPrivKey, order, ctx);
	BN_free(order);
	
	EC_KEY_set_private_key(pkey, privKey);
	
	EC_POINT* pubKey=EC_POINT_new(EC_KEY_get0_group(pkey));
	EC_POINT_mul(EC_KEY_get0_group(pkey), pubKey, privKey, NULL, NULL, ctx);
	BN_free(privKey);
	EC_KEY_set_public_key(pkey, pubKey);

	EC_POINT_free(pubKey);
	BN_CTX_free(ctx);

	return pkey;	
}
