
#include <string>

#include "boost/lexical_cast.hpp"

#include "Wallet.h"
#include "NewcoinAddress.h"
#include "Application.h"


LocalAccountEntry::LocalAccountEntry(const uint160& accountFamily, int accountSeq, EC_POINT* rootPubKey) :
 mAccountFamily(accountFamily), mAccountSeq(accountSeq),
 mPublicKey(new CKey(accountFamily, rootPubKey, accountSeq)),
 mBalance(0), mLedgerSeq(0), mTxnSeq(0)
{
	mAcctID=mPublicKey->GetAddress().GetHash160();
	mPublicKey=theApp->getPubKeyCache().store(mAcctID, mPublicKey);
}

void LocalAccountEntry::unlock(BIGNUM* rootPrivKey)
{
	if((mPrivateKey==NULL) && (rootPrivKey!=NULL))
		mPrivateKey=CKey::pointer(new CKey(mAccountFamily, rootPrivKey, mAccountSeq));
}             

std::string LocalAccountEntry::getAccountName() const
{
 return mPublicKey->GetAddress().GetString();
}

std::string LocalAccountEntry::getLocalAccountName() const
{
 return NewcoinAddress(mAccountFamily).GetString() + ":" +  boost::lexical_cast<std::string>(mAccountSeq);
}

LocalAccountFamily::LocalAccountFamily(const uint160& family, EC_POINT* pubKey) :
	mFamily(family), rootPubKey(pubKey), rootPrivateKey(NULL)
{ ; }

LocalAccountFamily::~LocalAccountFamily()
{
	lock();
	if(rootPubKey!=NULL) EC_POINT_free(rootPubKey);
}

uint160 LocalAccountFamily::getAccount(int seq)
{
	std::map<int, LocalAccountEntry::pointer>::iterator ait=mAccounts.find(seq);
	if(ait!=mAccounts.end()) return ait->second->getAccountID();

	LocalAccountEntry::pointer lae(new LocalAccountEntry(mFamily, seq, rootPubKey));
	mAccounts.insert(std::make_pair(seq, lae));

	return lae->getAccountID();
}

void LocalAccountFamily::unlock(BIGNUM* privateKey)
{
	if(rootPrivateKey!=NULL) BN_free(rootPrivateKey);
	rootPrivateKey=privateKey;
}

void LocalAccountFamily::lock()
{
	if(rootPrivateKey!=NULL)
	{
 		BN_free(rootPrivateKey);
		rootPrivateKey=NULL;
		for(std::map<int, LocalAccountEntry::pointer>::iterator it=mAccounts.begin(); it!=mAccounts.end(); ++it)
			it->second->lock();
	}
}

LocalAccountEntry::pointer LocalAccountFamily::get(int seq)
{
	std::map<int, LocalAccountEntry::pointer>::iterator act=mAccounts.find(seq);
	if(act!=mAccounts.end()) return act->second;

	LocalAccountEntry::pointer ret(new LocalAccountEntry(mFamily, seq, rootPubKey));
	mAccounts.insert(std::make_pair(seq, ret));
	return ret;
}

uint160 Wallet::addFamily(const std::string& payPhrase, bool lock)
{
	return doPrivate(CKey::PassPhraseToKey(payPhrase), true, !lock);
}

bool Wallet::addFamily(const uint160& familyName, const std::string& pubKey)
{
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=families.find(familyName);
	if(fit!=families.end()) // already added
		return true;

	EC_GROUP *grp=EC_GROUP_new_by_curve_name(NID_secp256k1);
	if(!grp) return false;

	BIGNUM* pbn=NULL;
	BN_hex2bn(&pbn, pubKey.c_str());
	if(!pbn)
	{
		EC_GROUP_free(grp);
		return false;
	}
	
	EC_POINT* rootPub=EC_POINT_bn2point(grp, pbn, NULL, NULL);
	EC_GROUP_free(grp);
	BN_free(pbn);
	if(!rootPub)
	{
		assert(false);
		return false;
	}
	
	LocalAccountFamily::pointer fam(new LocalAccountFamily(familyName, rootPub));
	families.insert(std::make_pair(familyName, fam));
	return true;
}

std::string Wallet::getPubKey(const uint160& famBase)
{
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=families.find(famBase);
	if(fit==families.end()) return "";

	EC_GROUP *grp=EC_GROUP_new_by_curve_name(NID_secp256k1);
	if(!grp) return "";

	BIGNUM* pubBase=EC_POINT_point2bn(grp, fit->second->peekPubKey(), POINT_CONVERSION_COMPRESSED, NULL, NULL);
	EC_GROUP_free(grp);
	if(!pubBase) return "";
	char *hex=BN_bn2hex(pubBase);
	BN_free(pubBase);
	if(!hex) return "";
	std::string ret(hex);
	OPENSSL_free(hex);
	return ret;
}

uint160 LocalAccount::getAddress() const
{
	LocalAccountEntry::pointer la(mFamily->get(mSeq));
	if(!la) return uint160();
	return la->getAccountID();
}

uint32 LocalAccount::getAcctSeq() const
{
	LocalAccountEntry::pointer la(mFamily->get(mSeq));
	if(!la) return 0;
	return la->getAccountSeq();
}

uint64 LocalAccount::getBalance() const
{
	LocalAccountEntry::pointer la(mFamily->get(mSeq));
	if(!la) return 0;
	return la->getBalance();
}

CKey::pointer LocalAccount::getPublicKey()
{
	LocalAccountEntry::pointer la(mFamily->get(mSeq));
	if(!la) return CKey::pointer();
	return la->getPubKey();
}

CKey::pointer LocalAccount::getPrivateKey()
{
	LocalAccountEntry::pointer la(mFamily->get(mSeq));
	if(!la) return CKey::pointer();
	return la->getPrivKey();
}

void Wallet::load()
{
}

LocalAccount::pointer Wallet::getLocalAccount(const uint160& family, int seq)
{
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=families.find(family);
	if(fit==families.end()) return LocalAccount::pointer();
	uint160 acct=fit->second->getAccount(seq);
	
	std::map<uint160, LocalAccount::pointer>::iterator ait=accounts.find(acct);
	if(ait!=accounts.end()) return ait->second;
	
	LocalAccount::pointer lac(new LocalAccount(fit->second, seq));
	accounts.insert(std::make_pair(acct, lac));
	return lac;
}

uint160 Wallet::doPrivate(const uint256& passPhrase, bool create, bool unlock)
{
// Generate root key
	EC_KEY *base=CKey::GenerateRootDeterministicKey(passPhrase);

// Extract family name
	std::vector<unsigned char> rootPubKey(33, 0);
	unsigned char *begin=&rootPubKey[0];
	i2o_ECPublicKey(base, &begin);
	while(rootPubKey.size()<33) rootPubKey.push_back((unsigned char)0);
	uint160 family=NewcoinAddress(rootPubKey).GetHash160();
	
	LocalAccountFamily::pointer fam;
	std::map<uint160, LocalAccountFamily::pointer>::iterator it=families.find(family);
	if(it==families.end())
	{ // family not found
		if(!create)
		{
			EC_KEY_free(base);
			return family;
		}
		fam=LocalAccountFamily::pointer(new LocalAccountFamily(family,
			EC_POINT_dup(EC_KEY_get0_public_key(base), EC_KEY_get0_group(base))));
		families[family]=fam;
	}
	else fam=it->second;

	if(unlock && fam->isLocked())
		fam->unlock(BN_dup(EC_KEY_get0_private_key(base)));
	
	EC_KEY_free(base);
	return family;
}
