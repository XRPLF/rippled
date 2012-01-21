#ifndef __WALLET__
#define __WALLET__

#include <vector>
#include <map>
#include <string>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>

#include "openssl/ec.h"

#include "json/value.h"

#include "uint256.h"
#include "Serializer.h"
#include "LocalAccount.h"
#include "LocalTransaction.h"

class Ledger;

class Wallet
{
protected:
	boost::recursive_mutex mLock;

	std::map<uint160, LocalAccountFamily::pointer> mFamilies;
	std::map<uint160, LocalAccount::pointer> mAccounts;
	std::map<uint256, LocalTransaction::pointer> mTransactions;

	uint32 mLedger; // ledger we last synched to

	LocalAccountFamily::pointer doPrivate(const uint256& passPhrase, bool do_create, bool do_unlock);
	LocalAccountFamily::pointer doPublic(const std::string& pubKey, bool do_create, bool do_db);

	void addFamily(const uint160& family, const std::string& pubKey, int seq,
		const std::string& name, const std::string& comment);

public:
	Wallet() : mLedger(0) { ; }

	uint160 addFamily(const std::string& passPhrase, bool lock);
	uint160 addFamily(const uint256& passPhrase, bool lock);
	uint160 addFamily(const std::string& pubKey);
	uint160 addRandomFamily(uint256& privKey);

	uint160 findFamilySN(const std::string& shortName);
	uint160 findFamilyPK(const std::string& pubKey);

	void delFamily(const uint160& familyName);

	void getFamilies(std::vector<uint160>& familyIDs);

	uint160 unlock(const uint256& passPhrase);
	bool lock(const uint160& familyName);
	void lock();

	void load();

	// must be a known local account
	LocalAccount::pointer parseAccount(const std::string& accountSpecifier);

	LocalAccount::pointer getLocalAccount(const uint160& famBase, int seq);
	LocalAccount::pointer getLocalAccount(const uint160& acctID);
	LocalAccount::pointer getNewLocalAccount(const uint160& family);
	LocalAccount::pointer findAccountForTransaction(uint64 amount);
	uint160 peekKey(const uint160& family, int seq);
	std::string getPubGenHex(const uint160& famBase);
	std::string getShortName(const uint160& famBase);

	bool getFamilyInfo(const uint160& family, std::string& name, std::string& comment);
	bool getFullFamilyInfo(const uint160& family, std::string& name, std::string& comment,
		std::string& pubGen, bool& isLocked);

	Json::Value getFamilyJson(const uint160& family);
	bool getTxJson(const uint256& txid, Json::Value& value);
	void addLocalTransactions(Json::Value&);

	static bool isHexPrivateKey(const std::string&);
	static bool isHexPublicKey(const std::string&);
	static bool isHexFamily(const std::string&);
	static std::string privKeyToText(const uint256& privKey);
	static uint256 textToPrivKey(const std::string&);

	void syncToLedger(bool force, Ledger* ledger);
	void applyTransaction(Transaction::pointer);

	static bool unitTest();
};

#endif
