#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include "DeterministicKeys.h"
#include "Serializer.h"

DetKeySet::DetKeySet(const std::string& phrase)
{
	Serializer s;
	s.addRaw((const void *) phrase.c_str(), phrase.length());
	mBase=s.getSHA512Half();
	s.secureErase();
}

static EC_KEY* GenerateDeterministicKey(const uint256& base, int n)
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
		BN_free(order);
		EC_KEY_free(pkey);
		BN_CTX_free(ctx);
		return NULL;
	}

	BIGNUM *privKey=NULL;
	int seq=0;
	do
	{ // private key must be non-zero and less than the curve's order
		if(privKey!=NULL) BN_free(privKey);
		Serializer s;
		s.add32(n);
		s.add256(base);
		s.add32(seq++);
		uint256 root=s.getSHA512Half();
		privKey=BN_bin2bn((const unsigned char *) &root, sizeof(root), NULL);
		memset(&root, 0, sizeof(root));
	} while(BN_is_zero(privKey) || (BN_cmp(privKey, order)>0));

	BN_free(order);

	if(!EC_KEY_set_private_key(pkey, privKey))
	{
		BN_free(privKey);
		BN_CTX_free(ctx);
		return NULL;
	}

	EC_POINT *pubKey=EC_POINT_new(EC_KEY_get0_group(pkey));
	if(!EC_POINT_mul(EC_KEY_get0_group(pkey), pubKey, privKey, NULL, NULL, ctx))
	{
		BN_free(privKey);
		EC_POINT_free(pubKey);
		BN_CTX_free(ctx);
		return NULL;
	}
	BN_free(privKey);
	if(!EC_KEY_set_public_key(pkey, pubKey))
	{
		EC_POINT_free(pubKey);
		BN_CTX_free(ctx);
		return NULL;
	}
	EC_POINT_free(pubKey);

	BN_CTX_free(ctx);

	assert(EC_KEY_check_key(pkey)==1);

	return pkey;
}

bool DetKeySet::getRandom(uint256& r)
{
	return RAND_bytes((unsigned char *) &r, sizeof(uint256)) == 1;
}

CKey::pointer DetKeySet::getPubKey(uint32 n)
{
	EC_KEY *k=GenerateDeterministicKey(mBase, n);
	if(k==NULL) return CKey::pointer();

	unsigned int size=i2o_ECPublicKey(k, NULL);
	std::vector<unsigned char> pubKey(size, 0);
	unsigned char* begin=&pubKey[0];
	i2o_ECPublicKey(k, &begin);
	EC_KEY_free(k);

	CKey::pointer ret(new CKey());
	ret->SetPubKey(pubKey);
	return ret;
}

CKey::pointer DetKeySet::getPrivKey(uint32 n)
{
	EC_KEY *k=GenerateDeterministicKey(mBase, n);
	if(k==NULL) return CKey::pointer();

	unsigned int size=i2d_ECPrivateKey(k, NULL);
	std::vector<unsigned char> privKey(size, 0);
	unsigned char* begin=&privKey[0];
	i2d_ECPrivateKey(k, &begin);
	EC_KEY_free(k);

	CKey::pointer ret(new CKey());
	ret->SetPrivKey(privKey);
	memset(&privKey[0], 0, privKey.size());
	return ret;
}

void DetKeySet::getPubKeys(uint32 first, uint32 count, std::list<CKey::pointer>& keys)
{
 while(count-->0)
  keys.push_back(getPubKey(first++));
}

void DetKeySet::getPrivKeys(uint32 first, uint32 count, std::list<CKey::pointer>& keys)
{
 while(count-->0)
  keys.push_back(getPrivKey(first++));
}

void DetKeySet::unitTest()
{
	uint256 u;
	GenerateDeterministicKey(u, 0);
	GenerateDeterministicKey(u, 1);
	u++;
	GenerateDeterministicKey(u, 0);
	GenerateDeterministicKey(u, 1);
}
