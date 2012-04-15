#ifndef __WALLET__
#define __WALLET__

#include <vector>
#include <map>
#include <string>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>

#include "openssl/ec.h"

#include "../json/value.h"

#include "uint256.h"
#include "Serializer.h"
#include "LocalAccount.h"
#include "LocalTransaction.h"

class Ledger;

class Wallet
{
private:
	bool	nodeIdentityLoad();
	bool	nodeIdentityCreate();

protected:
	boost::recursive_mutex mLock;

	NewcoinAddress	mNodePublicKey;
	NewcoinAddress	mNodePrivateKey;

	std::map<NewcoinAddress, LocalAccountFamily::pointer> mFamilies;
	std::map<NewcoinAddress, LocalAccount::pointer> mAccounts;
	std::map<uint256, LocalTransaction::pointer> mTransactions;

	uint32 mLedger; // ledger we last synched to

	LocalAccountFamily::pointer doPrivate(const NewcoinAddress& familySeed, bool do_create, bool do_unlock);
	LocalAccountFamily::pointer doPublic(const NewcoinAddress& familyGenerator, bool do_create, bool do_db);

	// void addFamily(const NewcoinAddress& family, const std::string& pubKey, int seq, const std::string& name, const std::string& comment);

public:
	Wallet();

	// Begin processing.
	// - Maintain peer connectivity through validation and peer management.
	void start();

	NewcoinAddress addFamily(const std::string& passPhrase, bool lock);
	NewcoinAddress addFamily(const NewcoinAddress& familySeed, bool lock);
	NewcoinAddress addFamily(const NewcoinAddress& familyGenerator);
	NewcoinAddress addRandomFamily(NewcoinAddress& familySeed);

	NewcoinAddress findFamilyPK(const NewcoinAddress& familyGenerator);

	void delFamily(const NewcoinAddress& familyName);

	void getFamilies(std::vector<NewcoinAddress>& familyIDs);

	bool lock(const NewcoinAddress& familyName);
	void lock();

	void load();

	// must be a known local account
	LocalAccount::pointer parseAccount(const std::string& accountSpecifier);

	LocalAccount::pointer getLocalAccount(const NewcoinAddress& famBase, int seq);
	LocalAccount::pointer getLocalAccount(const NewcoinAddress& acctID);
	LocalAccount::pointer getNewLocalAccount(const NewcoinAddress& family);
	LocalAccount::pointer findAccountForTransaction(uint64 amount);
	NewcoinAddress peekKey(const NewcoinAddress& family, int seq);

	bool getFamilyInfo(const NewcoinAddress& family, std::string& comment);
	// bool getFullFamilyInfo(const NewcoinAddress& family, std::string& comment, std::string& pubGen, bool& isLocked);

	Json::Value getFamilyJson(const NewcoinAddress& family);
	bool getTxJson(const uint256& txid, Json::Value& value);
	bool getTxsJson(const NewcoinAddress& acctid, Json::Value& value);
	void addLocalTransactions(Json::Value&);

	void syncToLedger(bool force, Ledger* ledger);
	void applyTransaction(Transaction::pointer);

	static bool unitTest();
};

#endif
