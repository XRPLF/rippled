
#include <string>

#include "openssl/rand.h"
#include "openssl/ec.h"

#include "boost/foreach.hpp"
#include "boost/lexical_cast.hpp"

#include "Wallet.h"
#include "NewcoinAddress.h"
#include "Application.h"

// TEMPORARY
#ifndef CHECK_NEW_FAMILIES
#define CHECK_NEW_FAMILIES 500
#endif

LocalAccountEntry::LocalAccountEntry(const uint160& accountFamily, int accountSeq, EC_POINT* rootPubKey) :
 mPublicKey(new CKey(accountFamily, rootPubKey, accountSeq)),
 mAccountFamily(accountFamily), mAccountSeq(accountSeq),
 mBalance(0), mLedgerSeq(0), mTxnSeq(0)
{
	mAcctID=mPublicKey->GetAddress().GetHash160();
	if(theApp!=NULL) mPublicKey=theApp->getPubKeyCache().store(mAcctID, mPublicKey);
}

std::string LocalAccountEntry::getAccountName() const
{
	return mPublicKey->GetAddress().GetString();
}

std::string LocalAccountEntry::getLocalAccountName() const
{
	return NewcoinAddress(mAccountFamily).GetString() + ":" +  boost::lexical_cast<std::string>(mAccountSeq);
}

LocalAccountFamily::LocalAccountFamily(const uint160& family, const EC_GROUP* group, const EC_POINT* pubKey) :
	mFamily(family), mLastSeq(0), mRootPrivateKey(NULL)
{
	mRootPubKey=EC_POINT_dup(pubKey, group);
	mName=family.GetHex().substr(0, 4);
}

LocalAccountFamily::~LocalAccountFamily()
{
	lock();
	if(mRootPubKey!=NULL) EC_POINT_free(mRootPubKey);
}

uint160 LocalAccountFamily::getAccount(int seq, bool keep)
{
	std::map<int, LocalAccountEntry::pointer>::iterator ait=mAccounts.find(seq);
	if(ait!=mAccounts.end()) return ait->second->getAccountID();

	LocalAccountEntry::pointer lae(new LocalAccountEntry(mFamily, seq, mRootPubKey));
	mAccounts.insert(std::make_pair(seq, lae));

	return lae->getAccountID();
}

void LocalAccountFamily::unlock(const BIGNUM* privateKey)
{
	if(mRootPrivateKey==NULL) mRootPrivateKey=BN_dup(privateKey);

#ifdef CHECK_NEW_FAMILIES
	std::cerr << "CheckingFamily" << std::endl;
	for(int i=0; i<CHECK_NEW_FAMILIES; i++)
	{
		EC_KEY *pubkey=CKey::GeneratePublicDeterministicKey(mFamily, mRootPubKey, i);
		EC_KEY *privkey=CKey::GeneratePrivateDeterministicKey(mFamily, mRootPrivateKey, i);

		int cmp=EC_POINT_cmp(EC_KEY_get0_group(pubkey), EC_KEY_get0_public_key(pubkey),
			EC_KEY_get0_public_key(privkey), NULL);
		if(cmp!=0)
		{
			std::cerr << BN_bn2hex(mRootPrivateKey) << std::endl;
			std::cerr << "family=" << mFamily.GetHex() << std::endl;
			std::cerr << "i=" << i << std::endl;
			std::cerr << "Key mismatch" << std::endl;
			assert(false);
		}

		EC_KEY_free(pubkey);
		EC_KEY_free(privkey);
	}
#endif
}

void LocalAccountFamily::lock()
{
	if(mRootPrivateKey!=NULL)
	{
 		BN_free(mRootPrivateKey);
		mRootPrivateKey=NULL;
	}
}

CKey::pointer LocalAccountFamily::getPrivateKey(int seq)
{
	if(!mRootPrivateKey) return CKey::pointer();
	return CKey::pointer(new CKey(mFamily, mRootPrivateKey, seq));
}

std::string LocalAccountFamily::getPubGenHex() const
{
	EC_GROUP *grp=EC_GROUP_new_by_curve_name(NID_secp256k1);
	if(!grp) return "";

	BIGNUM* pubBase=EC_POINT_point2bn(grp, mRootPubKey, POINT_CONVERSION_COMPRESSED, NULL, NULL);
	EC_GROUP_free(grp);

	if(!pubBase) return "";
	char *hex=BN_bn2hex(pubBase);
	BN_free(pubBase);

	if(!hex) return "";
	std::string ret(hex);
	OPENSSL_free(hex);

	return ret;
}

static bool isHex(char j)
{
	if((j>='0') && (j<='9')) return true;
	if((j>='A') && (j<='F')) return true;
	if((j>='a') && (j<='f')) return true;
	return false;
}

Json::Value LocalAccountFamily::getJson() const
{
	Json::Value ret(Json::objectValue);
	ret["ShortName"]=getShortName();
	ret["FullName"]=getFamily().GetHex();
	ret["PublicGenerator"]=getPubGenHex();
	ret["IsLocked"]=isLocked();
	if(!getComment().empty()) ret["Comment"]=getComment();
	return ret;
}

LocalAccountFamily::pointer LocalAccountFamily::readFamily(const uint160& family)
{
	std::string sql="SELECT * from LocalAcctFamilies WHERE FamilyName='";
	sql.append(family.GetHex());
	sql.append("';");

	std::string rootPubKey, name, comment;
	uint32 seq;

	if(1)
	{
		ScopedLock sl(theApp->getWalletDB()->getDBLock());
		Database *db=theApp->getWalletDB()->getDB();

		if(!db->executeSQL(sql.c_str()) || !db->startIterRows() || !db->getNextRow())
			return LocalAccountFamily::pointer();

		db->getStr("RootPubKey", rootPubKey);
		db->getStr("Name", name);
		db->getStr("Comment", comment);
		seq=db->getBigInt("Seq");

		db->endIterRows();
	}

	EC_GROUP *grp=EC_GROUP_new_by_curve_name(NID_secp256k1);
	if(!grp) return LocalAccountFamily::pointer();

	EC_POINT *pubKey=EC_POINT_hex2point(grp, rootPubKey.c_str(), NULL, NULL);
	if(!pubKey)
	{
		EC_GROUP_free(grp);
		assert(false);
		return LocalAccountFamily::pointer();
	}

	LocalAccountFamily::pointer fam(new LocalAccountFamily(family, grp, pubKey));
	EC_GROUP_free(grp);
	EC_POINT_free(pubKey);

	fam->setName(name);
	fam->setComment(comment);
	fam->setSeq(seq);
	return fam;
}

void LocalAccountFamily::write(bool is_new)
{
	std::string sql="INSERT INTO LocalAcctFamilies (FamilyName,RootPubKey,Seq,Name,Comment) VALUES ('";
	sql.append(mFamily.GetHex());
	sql.append("','");
	
	EC_GROUP* grp=EC_GROUP_new_by_curve_name(NID_secp256k1);
	if(!grp) return;
	char *rpk=EC_POINT_point2hex(grp, mRootPubKey, POINT_CONVERSION_COMPRESSED, NULL);
	EC_GROUP_free(grp);
	if(!rpk) return;
	sql.append(rpk);
	OPENSSL_free(rpk);

	sql.append("','");
	sql.append(boost::lexical_cast<std::string>(mLastSeq));
	sql.append("',");
	
	std::string f;
	theApp->getWalletDB()->getDB()->escape((const unsigned char *) mName.c_str(), mName.size(), f);
	sql.append(f);
	sql.append(",");
	theApp->getWalletDB()->getDB()->escape((const unsigned char *) mComment.c_str(), mComment.size(), f);
	sql.append(f);
	
	sql.append(");");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	theApp->getWalletDB()->getDB()->executeSQL(sql.c_str());
}

bool Wallet::isHexPrivateKey(const std::string& s)
{ // 65 characters, first is 'P', rest are all legal hex
	if(s.size()!=65) return false;
	if(s[0]!='P') return false;
	for(int i=1; i<65; i++)
		if(!isHex(s[i])) return false;
	return true;
}

bool Wallet::isHexPublicKey(const std::string& s)
{ // 66 characters, all legal hex, starts with '02' or '03'
	if(s.size()!=66) return false;
	if(s[0]!='0') return false;
	if((s[1]!='2') && (s[1]!='3')) return false;
	for(int i=3; i<66; i++)
		if(!isHex(s[i])) return false;
	return true;
}

bool Wallet::isHexFamily(const std::string& s)
{ // 64 characters, all legal hex
	if(s.size()!=64) return false;
	for(int i=0; i<64; i++)
		if(!isHex(s[i])) return false;
	return true;
}

std::string Wallet::privKeyToText(const uint256& privKey)
{
	std::string ret("P");
	ret.append(privKey.GetHex());
	return ret;
}

uint256 Wallet::textToPrivKey(const std::string& privKey)
{
	uint256 ret;
	if((privKey.length()==65) && (privKey[0]=='P'))
		ret.SetHex(privKey.c_str()+1);
	return ret;
}

std::string LocalAccountFamily::getSQLFields()
{
	return "(FamilyName,RootPubKey,Seq,Name,Comment)";
}

std::string LocalAccountFamily::getSQL() const
{ // familyname(40), pubkey(66), seq, name, comment
	std::string ret("('");
	ret.append(mFamily.GetHex());
	ret+="','";
	ret.append(getPubGenHex());
	ret.append("','");
	ret.append(boost::lexical_cast<std::string>(mLastSeq));
	ret.append("',");
	
	std::string esc;
	theApp->getWalletDB()->getDB()->escape((const unsigned char *) mName.c_str(), mName.size(), esc);
	ret.append(esc);
	ret.append(",");
	theApp->getWalletDB()->getDB()->escape((const unsigned char *) mComment.c_str(), mComment.size(), esc);
	ret.append(esc);

	ret.append(")");
	return ret;
}

LocalAccountEntry::pointer LocalAccountFamily::get(int seq)
{
	std::map<int, LocalAccountEntry::pointer>::iterator act=mAccounts.find(seq);
	if(act!=mAccounts.end()) return act->second;

	LocalAccountEntry::pointer ret(new LocalAccountEntry(mFamily, seq, mRootPubKey));
	mAccounts.insert(std::make_pair(seq, ret));
	return ret;
}

uint160 Wallet::findFamilySN(const std::string& shortName)
{ // OPTIMIZEME
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(std::map<uint160, LocalAccountFamily::pointer>::iterator it=mFamilies.begin();
		it!=mFamilies.end(); ++it)
	{
		if(it->second->getShortName()==shortName)
			return it->first;
	}
	return 0;
}

uint160 Wallet::addFamily(const uint256& key, bool lock)
{
	LocalAccountFamily::pointer fam(doPrivate(key, true, !lock));
	if(!fam) return uint160();
	return fam->getFamily();
}

uint160 Wallet::addRandomFamily(uint256& key)
{
	RAND_bytes((unsigned char *) &key, sizeof(key));
	return addFamily(key, false);
}

uint160 Wallet::addFamily(const std::string& payPhrase, bool lock)
{
	return addFamily(CKey::PassPhraseToKey(payPhrase), lock);
}

uint160 Wallet::addFamily(const std::string& pubKey)
{
	LocalAccountFamily::pointer fam(doPublic(pubKey, true, true));
	if(!fam) return uint160();
	return fam->getFamily();
}

uint160 Wallet::findFamilyPK(const std::string& pubKey)
{
	LocalAccountFamily::pointer fam(doPublic(pubKey, false, true));
	if(!fam) return uint160();
	return fam->getFamily();
}

uint160 LocalAccount::getAddress() const
{
	LocalAccountEntry::pointer la(mFamily->get(mSeq));
	if(!la) return uint160();
	return la->getAccountID();
}

std::string LocalAccount::getShortName() const
{
	std::string ret(mFamily->getShortName());
	ret.append(":");
	ret.append(boost::lexical_cast<std::string>(mSeq));
	return ret;
}

std::string LocalAccount::getFullName() const
{
	std::string ret(mFamily->getFamily().GetHex());
	ret.append(":");
	ret.append(boost::lexical_cast<std::string>(mSeq));
	return ret;
}

bool LocalAccount::isLocked() const
{
	return mFamily->isLocked();
}

std::string LocalAccount::getFamilyName() const
{
	return mFamily->getShortName();
}

Json::Value LocalAccount::getJson() const
{
	Json::Value ret(Json::objectValue);
	ret["Family"]=getFamilyName();
	ret["AccountID"]=NewcoinAddress(getAddress()).GetString();
	ret["ShortName"]=getShortName();
	ret["FullName"]=getFullName();
	ret["Issued"]=Json::Value(isIssued());
	ret["IsLocked"]=mFamily->isLocked();
	return ret;
}

bool LocalAccount::isIssued() const
{
	return mSeq < mFamily->getSeq();
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
	return mFamily->getPrivateKey(mSeq);
}

void Wallet::getFamilies(std::vector<uint160>& familyIDs)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	familyIDs.reserve(mFamilies.size());
	for(std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.begin(); fit!=mFamilies.end(); ++fit)
		familyIDs.push_back(fit->first);
}

bool Wallet::getFamilyInfo(const uint160& family, std::string& name, std::string& comment)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return false;
	assert(fit->second->getFamily()==family);
	name=fit->second->getShortName();
	comment=fit->second->getComment();
	return true;
}

bool Wallet::getFullFamilyInfo(const uint160& family, std::string& name, std::string& comment,
	std::string& pubGen, bool& isLocked)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return false;
	assert(fit->second->getFamily()==family);
	name=fit->second->getShortName();
	comment=fit->second->getComment();
	pubGen=fit->second->getPubGenHex();
	isLocked=fit->second->isLocked();
	return true;
}

Json::Value Wallet::getFamilyJson(const uint160& family)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return Json::Value(Json::nullValue);
	assert(fit->second->getFamily()==family);
	return fit->second->getJson();
}

void Wallet::load()
{
	std::string sql("SELECT * FROM LocalAcctFamilies;");
	
	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	Database *db=theApp->getWalletDB()->getDB();
	if(!db->executeSQL(sql.c_str()) || !db->startIterRows())
	{
#ifdef DEBUG
		std::cerr << "Unable to load wallet" << std::endl;
#endif
		return;
	}

	while(db->getNextRow())
	{
		std::string family, rootpub, name, comment;
		db->getStr("FamilyName", family);
		db->getStr("RootPubKey", rootpub);
		db->getStr("Name", name);
		db->getStr("Comment", comment);
		int seq=db->getBigInt("Seq");
		
		uint160 fb;
		fb.SetHex(family);

		LocalAccountFamily::pointer f(doPublic(rootpub, true, false));
		if(f)
		{
			assert(f->getFamily()==fb);
			f->setSeq(seq);
			f->setName(name);
			f->setComment(comment);
		}
		else assert(false);
	}
	db->endIterRows();
}

std::string Wallet::getPubGenHex(const uint160& famBase)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(famBase);
	if(fit==mFamilies.end()) return "";
	assert(fit->second->getFamily()==famBase);
	return fit->second->getPubGenHex();
}

std::string Wallet::getShortName(const uint160& famBase)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(famBase);
	if(fit==mFamilies.end()) return "";
	assert(fit->second->getFamily()==famBase);
	return fit->second->getShortName();
}

LocalAccount::pointer Wallet::getNewLocalAccount(const uint160& family)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return LocalAccount::pointer();

	uint32 seq=fit->second->getSeq();
	uint160 acct=fit->second->getAccount(seq, true);
	fit->second->setSeq(seq+1); // FIXME: writeout new seq
	
	std::map<uint160, LocalAccount::pointer>::iterator ait=mAccounts.find(acct);
	if(ait!=mAccounts.end()) return ait->second;
	
	LocalAccount::pointer lac(new LocalAccount(fit->second, seq));
	mAccounts.insert(std::make_pair(acct, lac));
	return lac;
}

LocalAccount::pointer Wallet::getLocalAccount(const uint160& family, int seq)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return LocalAccount::pointer();
	uint160 acct=fit->second->getAccount(seq, true);
	
	std::map<uint160, LocalAccount::pointer>::iterator ait=mAccounts.find(acct);
	if(ait!=mAccounts.end()) return ait->second;
	
	LocalAccount::pointer lac(new LocalAccount(fit->second, seq));
	mAccounts.insert(std::make_pair(acct, lac));
	return lac;
}

LocalAccount::pointer Wallet::getLocalAccount(const uint160& acctID)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccount::pointer>::iterator ait=mAccounts.find(acctID);
	if(ait==mAccounts.end()) return LocalAccount::pointer();
	return ait->second;
}

LocalAccount::pointer Wallet::findAccountForTransaction(uint64 amount)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(std::map<uint160, LocalAccount::pointer>::iterator it=mAccounts.begin(); it!=mAccounts.end(); ++it)
		if(!it->second->isLocked() && (it->second->getBalance()>=amount) )
			return it->second;
	return LocalAccount::pointer();
}

LocalAccount::pointer Wallet::parseAccount(const std::string& specifier)
{ // <family>:<seq> or <acct_id>

	std::cerr << "Parse(" << specifier << ")" << std::endl;

	size_t colon=specifier.find(':');
	if(colon == std::string::npos)
	{
		std::cerr << "nocolon" << std::endl;
		NewcoinAddress na(specifier);
		if(!na.IsValid()) return LocalAccount::pointer();
		return getLocalAccount(na.GetHash160());
	}
	
	if (colon==0) return LocalAccount::pointer();
	
	std::string family=specifier.substr(0, colon);
	std::string seq=specifier.substr(colon+1);
	if(seq.empty()) return LocalAccount::pointer();

	std::cerr << "family(" << family << "), seq(" << seq << ")" << std::endl;

	uint160 f;
	if(isHexFamily(family))
		f.SetHex(family);
	else if(isHexPublicKey(family))
		f=findFamilyPK(family);
	else
		f=findFamilySN(family);
	if(!f) return LocalAccount::pointer();
	return getLocalAccount(f, boost::lexical_cast<int>(seq));
}

uint160 Wallet::peekKey(const uint160& family, int seq)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return uint160();
	return fit->second->getAccount(seq, false);
}

void Wallet::delFamily(const uint160& familyName)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(familyName);
	if(fit==mFamilies.end()) return;

	std::map<int, LocalAccountEntry::pointer>& acctMap=fit->second->getAcctMap();
	for(std::map<int, LocalAccountEntry::pointer>::iterator it=acctMap.begin(); it!=acctMap.end(); ++it)
		mAccounts.erase(it->second->getAccountID());
	
	mFamilies.erase(familyName);
}

LocalAccountFamily::pointer Wallet::doPublic(const std::string& pubKey, bool do_create, bool do_db)
{
// Generate root key
	EC_KEY *pkey=CKey::GenerateRootPubKey(pubKey);

// Extract family name
    std::vector<unsigned char> rootPubKey(33, 0);
    unsigned char *begin=&rootPubKey[0];
    i2o_ECPublicKey(pkey, &begin);
    while(rootPubKey.size()<33) rootPubKey.push_back((unsigned char)0);
    uint160 family=NewcoinAddress(rootPubKey).GetHash160();

	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit!=mFamilies.end()) // already added
	{
		EC_KEY_free(pkey);
		return fit->second;
	}
	if(!do_create)
	{
		EC_KEY_free(pkey);
		return LocalAccountFamily::pointer();
	}

	LocalAccountFamily::pointer fam;
	if(do_db)
	{
		fam=LocalAccountFamily::readFamily(family);
		if(fam) do_create=false;
	}
	if(do_create)
	{
		fam=LocalAccountFamily::pointer(new LocalAccountFamily(family,
			EC_KEY_get0_group(pkey), EC_KEY_get0_public_key(pkey)));
		mFamilies.insert(std::make_pair(family, fam));
		if(do_db) fam->write(true);
	}
	sl.unlock();
	
	EC_KEY_free(pkey);
	return fam;
}

LocalAccountFamily::pointer Wallet::doPrivate(const uint256& passPhrase, bool do_create, bool do_unlock)
{
// Generate root key
	EC_KEY *base=CKey::GenerateRootDeterministicKey(passPhrase);

// Extract family name
	std::vector<unsigned char> rootPubKey(33, 0);
	unsigned char *begin=&rootPubKey[0];
	i2o_ECPublicKey(base, &begin);
	while(rootPubKey.size()<33) rootPubKey.push_back((unsigned char)0);
	uint160 family=NewcoinAddress(rootPubKey).GetHash160();
	
	boost::recursive_mutex::scoped_lock sl(mLock);
	LocalAccountFamily::pointer fam;
	std::map<uint160, LocalAccountFamily::pointer>::iterator it=mFamilies.find(family);
	if(it==mFamilies.end())
	{ // family not found
		fam=LocalAccountFamily::readFamily(family);
		if(!fam)
		{
			if(!do_create)
			{
				EC_KEY_free(base);
				return LocalAccountFamily::pointer();
			}
			fam=LocalAccountFamily::pointer(new LocalAccountFamily(family,
				EC_KEY_get0_group(base), EC_KEY_get0_public_key(base)));
			mFamilies.insert(std::make_pair(family, fam));
			fam->write(true);
		}
	}
	else fam=it->second;

	if(do_unlock && fam->isLocked())
		fam->unlock(EC_KEY_get0_private_key(base));
	
	sl.unlock();
	EC_KEY_free(base);
	return fam;
}

bool Wallet::lock(const uint160& family)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return false;
    fit->second->lock();
    return true;
}

void Wallet::lock()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(std::map<uint160, LocalAccountFamily::pointer>::iterator fit=mFamilies.begin();
			fit!=mFamilies.end(); ++fit)
		fit->second->lock();
}

bool Wallet::unitTest()
{ // Create 100 keys for each of 1,000 families and ensure all keys match
	Wallet pub, priv;
	
	uint256 privBase(time(NULL)^(getpid()<<16));
//	privBase.SetHex("102031459e");
	
	for(int i=0; i<1000; i++, privBase++)
	{
		uint160 fam=priv.addFamily(privBase, false);

#ifdef DEBUG
		std::cerr << "Priv: " << privBase.GetHex() << " Fam: " << fam.GetHex() << std::endl;
#endif
		
		std::string pubGen=priv.getPubGenHex(fam);
#ifdef DEBUG
		std::cerr << "Pub: " << pubGen << std::endl;
#endif
		
		if(pub.addFamily(pubGen)!=fam)
		{
			assert(false);
			return false;
		}

		if(pub.getPubGenHex(fam)!=pubGen)
		{
#ifdef DEBUG
			std::cerr << std::endl;
			std::cerr << "PuK: " << pub.getPubGenHex(fam) << std::endl;
			std::cerr << "PrK: " << priv.getPubGenHex(fam) << std::endl;
			std::cerr << "Fam: " << fam.GetHex() << std::endl;
#endif
			assert(false);
			return false;
		}

		for(int j=0; j<100; j++)
		{
			LocalAccount::pointer lpub=pub.getLocalAccount(fam, j);
			LocalAccount::pointer lpriv=priv.getLocalAccount(fam, j);
			if(!lpub || !lpriv)
			{
				assert(false);
				return false;
			}
			uint160 lpuba=lpub->getAddress();
			uint160 lpriva=lpriv->getAddress();
#ifdef DEBUG
//			std::cerr << "pubA(" << j << "): " << lpuba.GetHex() << std::endl;
//			std::cerr << "prvA(" << j << "): " << lpriva.GetHex() << std::endl;
			std::cerr << "." << std::flush;
#endif
			if(!lpuba || (lpuba!=lpriva))
			{
				assert(false);
				return false;
			}
		}
		std::cerr << std::endl;
		
		pub.delFamily(fam);
		priv.delFamily(fam);
		
	}
	return true;
}
