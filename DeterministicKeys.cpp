#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

// Functions to add CKey support for deterministic EC keys

#include "Serializer.h"

uint256 CKey::GetBaseFromString(const std::string& phrase)
{
	Serializer s;
	s.addRaw((const void *) phrase.c_str(), phrase.length());
	uint256 base(s.getSHA512Half());
	s.secureErase();
	return base;
}

EC_KEY* CKey::GenerateDeterministicKey(const uint256& base, uint32 n, bool private_key)
{
	BN_CTX* ctx=BN_CTX_new();
	if(!ctx) return NULL;

	EC_KEY* pkey=EC_KEY_new_by_curve_name(NID_secp256k1);

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
		Serializer s;
		s.add32(n);
		s.add256(base);
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

	if(private_key && !EC_KEY_set_private_key(pkey, privKey))
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

uint256 CKey::GetRandomBase(void)
{
	uint256 r;
	return (RAND_bytes((unsigned char *) &r, sizeof(uint256)) == 1) ? r : 0;
}
