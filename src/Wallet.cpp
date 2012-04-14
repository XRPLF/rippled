#define WIN32_LEAN_AND_MEAN 

#include <string>

#include "openssl/ec.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/make_shared.hpp>

#include "Wallet.h"
#include "Ledger.h"
#include "NewcoinAddress.h"
#include "Application.h"

// TEMPORARY
#ifndef CHECK_NEW_FAMILIES
#define CHECK_NEW_FAMILIES 500
#endif

//
// LocalAccount - an account
//

LocalAccount::LocalAccount(boost::shared_ptr<LocalAccountFamily> family, int familySeq) :
	mPublicKey(family->getPublicKey(familySeq)), mFamily(family), mAccountFSeq(familySeq),
	mLgrBalance(0), mTxnDelta(0), mTxnSeq(0)
{
	mAcctID.setAccountPublic(mPublicKey->GetPubKey());

	if(theApp!=NULL) mPublicKey=theApp->getPubKeyCache().store(mAcctID, mPublicKey);
}

std::string LocalAccount::getFullName() const
{
	std::string ret(mFamily->getFamily().humanFamilyGenerator());
	ret.append(":");
	ret.append(boost::lexical_cast<std::string>(mAccountFSeq));

	return ret;
}

bool LocalAccount::isLocked() const
{
	return mFamily->isLocked();
}

std::string LocalAccount::getFamilyName() const
{
	return mFamily->getFamily().humanFamilyGenerator();
}

Json::Value LocalAccount::getJson() const
{
	Json::Value ret(Json::objectValue);
	ret["Family"]=getFamilyName();
	ret["AccountID"]=getAddress().humanAccountID();
	ret["AccountPublic"]=getAddress().humanAccountPublic();
	ret["FullName"]=getFullName();
	ret["Issued"]=Json::Value(isIssued());
	ret["IsLocked"]=mFamily->isLocked();

	uint64 eb=getEffectiveBalance();
	if(eb!=0) ret["Balance"]=boost::lexical_cast<std::string>(eb);

	uint32 sq=getTxnSeq();
	if(sq!=0) ret["TxnSeq"]=boost::lexical_cast<std::string>(sq);

	return ret;
}

bool LocalAccount::isIssued() const
{
	return mAccountFSeq < mFamily->getSeq();
}

CKey::pointer LocalAccount::getPrivateKey()
{
	return mFamily->getPrivateKey(mAccountFSeq);
}
//
// LocalAccountFamily - a sequences of accounts
//

LocalAccountFamily::LocalAccountFamily(const NewcoinAddress& familyGenerator) :
	mFamily(familyGenerator), mLastSeq(0), mRootPrivateKey(NULL)
{
}

LocalAccountFamily::~LocalAccountFamily()
{
	lock();
}

NewcoinAddress LocalAccountFamily::getAccount(int seq, bool keep)
{
	std::map<int, LocalAccount::pointer>::iterator ait=mAccounts.find(seq);
	if(ait!=mAccounts.end()) return ait->second->getAddress();

	LocalAccount::pointer lae=boost::make_shared<LocalAccount>(shared_from_this(), seq);
	mAccounts.insert(std::make_pair(seq, lae));

	return lae->getAddress();
}

void LocalAccountFamily::unlock(BIGNUM* privateKey)
{
	if(mRootPrivateKey==NULL) mRootPrivateKey=privateKey;

#ifdef CHECK_NEW_FAMILIES
	std::cerr << "CheckingFamily" << std::endl;
	for(int i=0; i<CHECK_NEW_FAMILIES; i++)
	{
		EC_KEY *pubkey=CKey::GeneratePublicDeterministicKey(mFamily, i);
		EC_KEY *privkey=CKey::GeneratePrivateDeterministicKey(mFamily, mRootPrivateKey, i);

		int cmp=EC_POINT_cmp(EC_KEY_get0_group(pubkey), EC_KEY_get0_public_key(pubkey),
			EC_KEY_get0_public_key(privkey), NULL);
		if(cmp!=0)
		{
			std::cerr << BN_bn2hex(mRootPrivateKey) << std::endl;
			std::cerr << "family=" << mFamily.humanFamilyGenerator() << std::endl;
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

CKey::pointer LocalAccountFamily::getPublicKey(int seq)
{
	return boost::make_shared<CKey>(mFamily, seq);
}

CKey::pointer LocalAccountFamily::getPrivateKey(int seq)
{
	if(!mRootPrivateKey) return CKey::pointer();
	std::cerr << "PrivKey(" << mFamily.humanFamilyGenerator() << "," << seq << ")" << std::endl;
	return boost::make_shared<CKey>(mFamily, mRootPrivateKey, seq);
}

Json::Value LocalAccountFamily::getJson() const
{
	Json::Value ret(Json::objectValue);
	ret["FullName"]=getFamily().humanFamilyGenerator();
	ret["IsLocked"]=isLocked();
	if(!getComment().empty()) ret["Comment"]=getComment();
	return ret;
}

LocalAccountFamily::pointer LocalAccountFamily::readFamily(const NewcoinAddress& family)
{
	std::string sql="SELECT * from LocalAcctFamilies WHERE FamilyGenerator='";
	sql.append(family.humanFamilyGenerator());
	sql.append("';");

	std::string comment;
	uint32 seq;

	if(1)
	{
		ScopedLock sl(theApp->getWalletDB()->getDBLock());
		Database *db=theApp->getWalletDB()->getDB();

		if(!db->executeSQL(sql.c_str()) || !db->startIterRows() || !db->getNextRow())
			return LocalAccountFamily::pointer();

		db->getStr("Comment", comment);
		seq=db->getBigInt("Seq");

		db->endIterRows();
	}

	LocalAccountFamily::pointer fam=boost::make_shared<LocalAccountFamily>(family);

	fam->setComment(comment);
	fam->setSeq(seq);

	return fam;
}

void LocalAccountFamily::write(bool is_new)
{
	std::string sql="INSERT INTO LocalAcctFamilies (FamilyGenerator,Seq,Comment) VALUES ('";
	sql.append(mFamily.humanFamilyGenerator());
	sql.append("','");

	sql.append(boost::lexical_cast<std::string>(mLastSeq));
	sql.append("',");

	std::string f;
	theApp->getWalletDB()->getDB()->escape((const unsigned char *) mComment.c_str(), mComment.size(), f);
	sql.append(f);

	sql.append(");");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	theApp->getWalletDB()->getDB()->executeSQL(sql.c_str());
}

std::string LocalAccountFamily::getSQLFields()
{
	return "(FamilyGenerator,Seq,Comment)";
}

std::string LocalAccountFamily::getSQL() const
{ // familyname(40), seq, comment
	std::string ret("('");
	ret.append(mFamily.humanFamilyGenerator());
	ret.append("','");
	ret.append(boost::lexical_cast<std::string>(mLastSeq));
	ret.append("',");

	std::string esc;
	theApp->getWalletDB()->getDB()->escape((const unsigned char *) mComment.c_str(), mComment.size(), esc);
	ret.append(esc);

	ret.append(")");
	return ret;
}

LocalAccount::pointer LocalAccountFamily::get(int seq)
{
	std::map<int, LocalAccount::pointer>::iterator act=mAccounts.find(seq);
	if(act!=mAccounts.end()) return act->second;

	LocalAccount::pointer ret=boost::make_shared<LocalAccount>(shared_from_this(), seq);
	mAccounts.insert(std::make_pair(seq, ret));

	return ret;
}

Wallet::Wallet() : mLedger(0) {
	mPtScoresUpdated	= boost::posix_time::from_time_t(0);
}

NewcoinAddress Wallet::addFamily(const NewcoinAddress& familySeed, bool lock)
{
	LocalAccountFamily::pointer fam(doPrivate(familySeed, true, !lock));

	return fam ? fam->getFamily() : NewcoinAddress();
}

// Create a family.  Return the family public generator and the seed.
NewcoinAddress Wallet::addRandomFamily(NewcoinAddress& familySeed)
{
	familySeed.setFamilySeedRandom();

	return addFamily(familySeed, false);
}

NewcoinAddress Wallet::addFamily(const std::string& payPhrase, bool lock)
{
	NewcoinAddress familySeed;

	familySeed.setFamilySeed(CKey::PassPhraseToKey(payPhrase));

	return addFamily(familySeed, lock);
}

NewcoinAddress Wallet::addFamily(const NewcoinAddress& familyGenerator)
{
	LocalAccountFamily::pointer fam(doPublic(familyGenerator, true, true));

	return fam ? fam->getFamily() : NewcoinAddress();
}

NewcoinAddress Wallet::findFamilyPK(const NewcoinAddress& familyGenerator)
{
	LocalAccountFamily::pointer fam(doPublic(familyGenerator, false, true));

	return fam ? fam->getFamily() : NewcoinAddress();
}

void Wallet::getFamilies(std::vector<NewcoinAddress>& familyIDs)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	familyIDs.reserve(mFamilies.size());
	for(std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.begin(); fit!=mFamilies.end(); ++fit)
		familyIDs.push_back(fit->first);
}

// Look up a family in the wallet.
bool Wallet::getFamilyInfo(const NewcoinAddress& family, std::string& comment)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end())
	    return false;

	assert(fit->second->getFamily()==family);

	comment=fit->second->getComment();

	return true;
}

#if 0
bool Wallet::getFullFamilyInfo(const NewcoinAddress& family, std::string& comment,
	std::string& pubGen, bool& isLocked)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return false;
	assert(fit->second->getFamily()==family);
	comment=fit->second->getComment();
	pubGen=fit->second->getPubGenHex();
	isLocked=fit->second->isLocked();
	return true;
}
#endif

Json::Value Wallet::getFamilyJson(const NewcoinAddress& family)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return Json::Value(Json::nullValue);
	assert(fit->second->getFamily()==family);
	return fit->second->getJson();
}

void Wallet::start()
{
	miscLoad();

	struct tm	tmScoresUpdated	= to_tm(mPtScoresUpdated);
	time_t		ttScoresUpdated	= mktime(&tmScoresUpdated);

	std::cerr << "Validator scores updated: "
		<< (ttScoresUpdated
			? "Never"
			: boost::posix_time::to_simple_string(mPtScoresUpdated)
			)
		<< std::endl;

	theApp->getUNL().start();
}

bool Wallet::miscLoad()
{
	std::string strSql("SELECT * FROM Misc;");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	Database *db=theApp->getWalletDB()->getDB();

	if(!db->executeSQL(strSql.c_str())) return false;
	if(!db->startIterRows()) return false;

	time_t	ttScoresUpdated	= db->getInt("ScoresUpdated");

	mPtScoresUpdated	= boost::posix_time::from_time_t(ttScoresUpdated);

	db->endIterRows();

	return true;
}

bool Wallet::miscSave()
{
	struct tm	tmScoresUpdated	= to_tm(mPtScoresUpdated);
	time_t		ttScoresUpdated	= mktime(&tmScoresUpdated);
	std::string	strScoresUpdate	= boost::lexical_cast<std::string>(ttScoresUpdated);

	std::string strSql("REPLACE INTO Misc (ScoresUpdated) VALUES (");
	// Should be a parameter.
	strSql.append(strScoresUpdate);
	strSql.append(");");

	Database*	db=theApp->getWalletDB()->getDB();
	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	db->executeSQL(strSql.c_str());

	return true;
}

bool Wallet::nodeIdentityLoad()
{
	std::string strSql("SELECT * FROM NodeIdentity;");

	Database*	db=theApp->getWalletDB()->getDB();
	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	if(!db->executeSQL(strSql.c_str())) return false;
	if(!db->startIterRows()) return false;

	std::string strPublicKey, strPrivateKey;

	db->getStr("PublicKey", strPublicKey);
	db->getStr("PrivateKey", strPrivateKey);

	mNodePublicKey.setNodePublic(strPublicKey);
	mNodePrivateKey.setNodePrivate(strPrivateKey);

	db->endIterRows();

	return true;
}

bool Wallet::nodeIdentityCreate() {
	//
	// Generate the public and private key
	//
	NewcoinAddress	familySeed;
	NewcoinAddress	familyGenerator;
	NewcoinAddress	nodePublicKey;
	NewcoinAddress	nodePrivateKey;

	familySeed.setFamilySeedRandom();					// Get a random seed.
	familyGenerator.setFamilyGenerator(familySeed);		// Derive generator from seed.

	// The node public and private is 0th of the sequence.
	nodePublicKey.setNodePublic(CKey(familyGenerator, 0).GetPubKey());
	nodePrivateKey.setNodePrivate(CKey(familyGenerator, familySeed.getFamilyPrivateKey(), 0).GetSecret());

	std::cerr << "New NodeIdentity:" << std::endl;
	fprintf(stderr, "public: %s\n", nodePublicKey.humanNodePublic().c_str());
	fprintf(stderr, "private: %s\n", nodePrivateKey.humanNodePrivate().c_str());

	//
	// Store the node information
	//
	Database* db	= theApp->getWalletDB()->getDB();

	std::string strTmp;

	std::string strPublicKey	= nodePublicKey.humanNodePublic();
	std::string strPrivateKey	= nodePrivateKey.humanNodePrivate();

	std::string strSql	= "INSERT INTO NodeIdentity (PublicKey,PrivateKey) VALUES (";
	db->escape(reinterpret_cast<const unsigned char*>(strPublicKey.c_str()), strPublicKey.size(), strTmp);
	strSql.append(strTmp);
	strSql.append(",");
	db->escape(reinterpret_cast<const unsigned char*>(strPrivateKey.c_str()), strPrivateKey.size(), strTmp);
	strSql.append(strTmp);
	strSql.append(");");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(strSql.c_str());

	return true;
}

void Wallet::load()
{
	if (!nodeIdentityLoad()) {
		nodeIdentityCreate();
		if (!nodeIdentityLoad())
			throw std::runtime_error("unable to retrieve new node identity.");
	}

	std::cerr << "NodeIdentity:" << std::endl;
	fprintf(stderr, "public: %s\n", mNodePublicKey.humanNodePublic().c_str());
	fprintf(stderr, "private: %s\n", mNodePrivateKey.humanNodePrivate().c_str());

	std::string sql("SELECT * FROM LocalAcctFamilies;");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	Database *db=theApp->getWalletDB()->getDB();
	if(!db->executeSQL(sql.c_str()))
	{
#ifdef DEBUG
		std::cerr << "Unable to load wallet" << std::endl;
#endif
		return;
	}

	if(!db->startIterRows()) return;

	while(db->getNextRow())
	{
		std::string strGenerator, strComment;

		db->getStr("FamilyGenerator", strGenerator);
		db->getStr("Comment", strComment);
		int seq=db->getBigInt("Seq");

		NewcoinAddress	familyGenerator;

		familyGenerator.setFamilyGenerator(strGenerator);

		LocalAccountFamily::pointer f(doPublic(familyGenerator, true, false));
		if(f)
		{
			assert(f->getFamily().getFamilyGenerator()==familyGenerator.getFamilyGenerator());
			f->setSeq(seq);
			f->setComment(strComment);
		}
		else assert(false);
	}
	db->endIterRows();
}

// YYY Perhaps this should take a comment.
LocalAccount::pointer Wallet::getNewLocalAccount(const NewcoinAddress& family)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return LocalAccount::pointer();

	uint32 seq=fit->second->getSeq();
	NewcoinAddress acct=fit->second->getAccount(seq, true);
	fit->second->setSeq(seq+1); // FIXME: writeout new seq

	std::map<NewcoinAddress, LocalAccount::pointer>::iterator ait=mAccounts.find(acct);
	if(ait!=mAccounts.end()) return ait->second;

	LocalAccount::pointer lac=boost::make_shared<LocalAccount>(fit->second, seq);
	mAccounts.insert(std::make_pair(acct, lac));

	sl.unlock();
	lac->syncLedger();

	return lac;
}

LocalAccount::pointer Wallet::getLocalAccount(const NewcoinAddress& family, int seq)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return LocalAccount::pointer();

	NewcoinAddress acct=fit->second->getAccount(seq, true);

	std::map<NewcoinAddress, LocalAccount::pointer>::iterator ait=mAccounts.find(acct);
	if(ait!=mAccounts.end()) return ait->second;

	LocalAccount::pointer lac=boost::make_shared<LocalAccount>(fit->second, seq);
	mAccounts.insert(std::make_pair(acct, lac));

	sl.unlock();
	lac->syncLedger();

	return lac;
}

LocalAccount::pointer Wallet::getLocalAccount(const NewcoinAddress& acctID)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccount::pointer>::iterator ait=mAccounts.find(acctID);
	if(ait==mAccounts.end()) return LocalAccount::pointer();
	return ait->second;
}

LocalAccount::pointer Wallet::findAccountForTransaction(uint64 amount)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(std::map<NewcoinAddress, LocalAccount::pointer>::iterator it=mAccounts.begin(); it!=mAccounts.end(); ++it)
		if(!it->second->isLocked() && (it->second->getEffectiveBalance()>=amount) )
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
		NewcoinAddress na;

		if (na.setAccountID(specifier))
		{
		    return getLocalAccount(na);
		}
		else
		{
		    return LocalAccount::pointer();
		}
	}

	if (colon==0) return LocalAccount::pointer();

	std::string family=specifier.substr(0, colon);
	std::string seq=specifier.substr(colon+1);

	if(seq.empty()) return LocalAccount::pointer();

	std::cerr << "family(" << family << "), seq(" << seq << ")" << std::endl;

	NewcoinAddress	familyParsed;
	NewcoinAddress	familyFound;

	if (familyParsed.setFamilyGenerator(family))
	{
		familyFound	= findFamilyPK(familyParsed);
	}
	if (familyParsed.setFamilySeed(family))
	{
		// XXX Was private generator, should derive public generator.
		;	// nothing
	}

	return familyFound.IsValid()
		? getLocalAccount(familyFound, boost::lexical_cast<int>(seq))
		: LocalAccount::pointer();
}

NewcoinAddress Wallet::peekKey(const NewcoinAddress& family, int seq)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return NewcoinAddress();

	return fit->second->getAccount(seq, false);
}

void Wallet::delFamily(const NewcoinAddress& familyName)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(familyName);
	if(fit==mFamilies.end()) return;

	std::map<int, LocalAccount::pointer>& acctMap=fit->second->getAcctMap();
	for(std::map<int, LocalAccount::pointer>::iterator it=acctMap.begin(); it!=acctMap.end(); ++it)
		mAccounts.erase(it->second->getAddress());

	mFamilies.erase(familyName);
}

// Look for and possible create a family based on its generator.
// --> pubKey: hex
// --> do_create: Add to mFamilies
// --> do_db: write out db
LocalAccountFamily::pointer Wallet::doPublic(const NewcoinAddress& familyGenerator, bool do_create, bool do_db)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(familyGenerator);
	if(fit!=mFamilies.end()) // already added
	{
		return fit->second;
	}
	if(!do_create)
	{
		return LocalAccountFamily::pointer();
	}

	LocalAccountFamily::pointer fam;
	if(do_db)
	{
		fam=LocalAccountFamily::readFamily(familyGenerator);
		if(fam) do_create=false;
	}

	if(do_create)
	{
		fam=boost::make_shared<LocalAccountFamily>(familyGenerator);
		mFamilies.insert(std::make_pair(familyGenerator, fam));
		if(do_db) fam->write(true);
	}
	sl.unlock();

	return fam;
}

// Look for and possible create a family based on its seed.
LocalAccountFamily::pointer Wallet::doPrivate(const NewcoinAddress& familySeed, bool do_create, bool do_unlock)
{
	NewcoinAddress familyGenerator;
	familyGenerator.setFamilyGenerator(familySeed);

	boost::recursive_mutex::scoped_lock sl(mLock);
	LocalAccountFamily::pointer fam;
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator it=mFamilies.find(familyGenerator);
	if(it==mFamilies.end())
	{ // family not found
		fam=LocalAccountFamily::readFamily(familyGenerator);
		if(!fam)
		{
			if(!do_create)
			{
				return LocalAccountFamily::pointer();
			}
			fam=boost::make_shared<LocalAccountFamily>(familyGenerator);
			mFamilies.insert(std::make_pair(familyGenerator, fam));
			fam->write(true);
		}
	}
	else fam=it->second;

	if(do_unlock && fam->isLocked())
		fam->unlock(familySeed.getFamilyPrivateKey());

	sl.unlock();

	return fam;
}

bool Wallet::lock(const NewcoinAddress& family)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.find(family);
	if(fit==mFamilies.end()) return false;
    fit->second->lock();
    return true;
}

void Wallet::lock()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(std::map<NewcoinAddress, LocalAccountFamily::pointer>::iterator fit=mFamilies.begin();
			fit!=mFamilies.end(); ++fit)
		fit->second->lock();
}

bool Wallet::unitTest()
{ // Create 100 keys for each of 1,000 families and ensure all keys match
#if 0
	Wallet pub, priv;

	uint256 privBase(time(NULL)^(getpid()<<16));
//	privBase.SetHex("102031459e");

	for(int i=0; i<1000; i++, privBase++)
	{
		NewcoinAddress fam=priv.addFamily(privBase, false);

#ifdef DEBUG
		std::cerr << "Priv: " << privBase.GetHex() << " Fam: " << fam.GetHex() << std::endl;
#endif

#if 0
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
#endif
		for(int j=0; j<100; j++)
		{
			LocalAccount::pointer lpub=pub.getLocalAccount(fam, j);
			LocalAccount::pointer lpriv=priv.getLocalAccount(fam, j);
			if(!lpub || !lpriv)
			{
				assert(false);
				return false;
			}
			NewcoinAddress lpuba=lpub->getAddress();
			NewcoinAddress lpriva=lpriv->getAddress();
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
#endif
	return true;
}

void Wallet::syncToLedger(bool force, Ledger* ledger)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(!force && (mLedger>=ledger->getLedgerSeq())) return;
	for(std::map<uint256, LocalTransaction::pointer>::iterator xit=mTransactions.begin();
			xit!=mTransactions.end(); ++xit)
	{ // check each transaction, see if it's in the ledger or allowed in the ledger
		// WRITEME
	}	
	for(std::map<NewcoinAddress, LocalAccount::pointer>::iterator ait=mAccounts.begin(); ait!=mAccounts.end(); ++ait)
	{ // check each account, see if our ledger balance matches
		LocalAccount::pointer& lac=ait->second;
		AccountState::pointer acs=ledger->getAccountState(ait->first);
		if(!acs) lac->setLedgerBalance(0);
		else lac->setLedgerBalance(acs->getBalance());
	}
	if(mLedger<ledger->getLedgerSeq()) mLedger=ledger->getLedgerSeq();
}

void Wallet::applyTransaction(Transaction::pointer txn)
{
	TransStatus st=txn->getStatus();
	bool shouldBePaid=(st==INCLUDED) || (st==HELD) || (st==NEW);

	LocalTransaction::pointer ltx;

	boost::recursive_mutex::scoped_lock sl(mLock);

	std::map<uint256, LocalTransaction::pointer>::iterator lti=mTransactions.find(txn->getID());
	if(lti!=mTransactions.end()) ltx=lti->second;

	std::map<NewcoinAddress, LocalAccount::pointer>::iterator lac=mAccounts.find(txn->getToAccount());
	if(lac!=mAccounts.end())
	{ // this is to a local account
		if(!ltx)
		{ // this is to a local account, and we don't have a local transaction for it
			ltx=boost::make_shared<LocalTransaction>
				(txn->getToAccount(), txn->getAmount(), txn->getIdent());
			ltx->setTransaction(txn);
			mTransactions.insert(std::make_pair<uint256, LocalTransaction::pointer>(txn->getID(), ltx));
			ltx->setCredited();
			lac->second->credit(txn->getAmount()-txn->getFee());
		}
	}

	lac=mAccounts.find(txn->getFromAccount());
	if(lac==mAccounts.end()) return;

	if ( (st!=INVALID) && (lac->second->getTxnSeq()==txn->getFromAccountSeq()) )
		lac->second->incTxnSeq();

	if(!ltx)
	{ // we don't have this transactions
		if(shouldBePaid)
		{ // we need it
			ltx=boost::make_shared<LocalTransaction>
				(txn->getToAccount(), txn->getAmount(), txn->getIdent());
			ltx->setTransaction(txn);
			mTransactions.insert(std::make_pair<uint256, LocalTransaction::pointer>(txn->getID(), ltx));
			lac->second->debit(txn->getAmount());
			ltx->setPaid();
		}
	}
	else
	{ // we have this transaction in some form (ltx->second)
		if( (st==COMMITTED) || (st==INVALID) )
		{ // we need to remove it
			if(ltx->isPaid())
			{
				lac->second->credit(txn->getAmount());
				ltx->setUnpaid();
			}
			mTransactions.erase(txn->getID());
		}
		else if(ltx->isPaid() && !shouldBePaid)
		{ // we paid for this transaction and it didn't happen
			lac->second->credit(txn->getAmount());
			ltx->setUnpaid();
		}
		else if(!ltx->isPaid() && shouldBePaid)
		{ // this transaction happened, and we haven't paid locally
			lac->second->debit(txn->getAmount());
			ltx->setPaid();
		}
	}
}

void Wallet::addLocalTransactions(Json::Value& ret)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(std::map<uint256, LocalTransaction::pointer>::iterator it=mTransactions.begin();
			it!=mTransactions.end(); ++it)
		ret[it->first.GetHex()]=it->second->getJson();
}

bool Wallet::getTxJson(const uint256& txn, Json::Value& ret)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::map<uint256, LocalTransaction::pointer>::iterator it=mTransactions.find(txn);
	if(it==mTransactions.end()) return false;
	ret=it->second->getJson();

	return true;
}

bool Wallet::getTxsJson(const NewcoinAddress& account, Json::Value& ret)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	for(std::map<uint256, LocalTransaction::pointer>::iterator it=mTransactions.begin();
			it!=mTransactions.end(); ++it)
	{
		Transaction::pointer txn=it->second->getTransaction();
		if(txn && ((account==txn->getFromAccount())||(account==txn->getToAccount())) )
			ret[it->first.GetHex()]=it->second->getJson();
	}

	return true;
}
// vim:ts=4
