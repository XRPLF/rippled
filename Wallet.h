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

class LocalAccountEntry
{ // tracks keys for local accounts
public:
	typedef boost::shared_ptr<LocalAccountEntry> pointer;

protected:
	// core account information
	CKey::pointer mPublicKey;
	uint160 mAcctID;
	std::string mName, mComment;

	// family information
	uint160 mAccountFamily;
	int mAccountSeq;

	// local usage tracking
	uint64 mBalance;		// The balance, last we checked/updated
	uint32 mLedgerSeq;		// The ledger seq when we updated the balance
	uint32 mTxnSeq;			// The sequence number of the next transaction

public:
	LocalAccountEntry(const uint160& accountFamily, int accountSeq, EC_POINT* rootPubKey);

	// Database operations
	bool read();			// reads any existing data
	bool write();			// creates the record in the first place
	bool updateName();		// writes changed name/comment
	bool updateBalance();	// writes changed balance/seq

	const uint160& getAccountID() const { return mAcctID; }
	int getAccountSeq() const { return mAccountSeq; }
	std::string getLocalAccountName() const;			// The name used locally to identify this account
	std::string getAccountName() const;					// The normal account name used to send to this account

	CKey::pointer getPubKey() { return mPublicKey; }

	void update(uint64 balance, uint32 seq);
	uint32 getTxnSeq() const { return  mTxnSeq; }
	uint32 incTxnSeq() { return mTxnSeq++; }

	uint64 getBalance() const { return mBalance; }
	void credit(uint64 amount) { mBalance+=amount; }
	void debit(uint64 amount) { assert(mBalance>=amount); mBalance-=amount; }
};

class LocalAccountFamily
{ // tracks families of local accounts
public:
	typedef boost::shared_ptr<LocalAccountFamily> pointer;

protected:
	std::map<int, LocalAccountEntry::pointer> mAccounts;

	uint160		mFamily;		// the name for this account family
	EC_POINT*	mRootPubKey;

	uint32 mLastSeq;
	std::string mName, mComment;

	BIGNUM*		mRootPrivateKey;

public:

	LocalAccountFamily(const uint160& family, const EC_GROUP* group, const EC_POINT* pubKey);
	~LocalAccountFamily();

	const uint160& getFamily() const { return mFamily; }

	void unlock(const BIGNUM* privateKey);
	void lock();
	bool isLocked() const { return mRootPrivateKey==NULL; }

	void setSeq(uint32 s) { mLastSeq=s; }
	uint32 getSeq() { return mLastSeq; }
	void setName(const std::string& n) { mName=n; }
	void setComment(const std::string& c) { mComment=c; }

	std::map<int, LocalAccountEntry::pointer>& getAcctMap() { return mAccounts; }
	LocalAccountEntry::pointer get(int seq);
	uint160 getAccount(int seq, bool keep);
	CKey::pointer getPrivateKey(int seq);

	std::string getPubGenHex() const;	// The text name of the public key
	std::string getShortName() const { return mName; }
	std::string getComment() const { return mComment; }
	Json::Value getJson() const;

	static std::string getSQLFields();
	std::string getSQL() const;
	static LocalAccountFamily::pointer readFamily(const uint160& family);
	void write(bool is_new);
};

class LocalAccount
{ // tracks a single local account in a form that can be passed to other code.
public:
	typedef boost::shared_ptr<LocalAccount> pointer;

protected:
	LocalAccountFamily::pointer mFamily;
	int mSeq;

public:
	LocalAccount(LocalAccountFamily::pointer fam, int seq) : mFamily(fam), mSeq(seq) { ; }
	uint160 getAddress() const;
	bool isLocked() const;

	std::string getShortName() const;
	std::string getFullName() const;
	std::string getFamilyName() const;
	bool isIssued() const;

	bool signRaw(Serializer::pointer);
	bool signRaw(Serializer::pointer, std::vector<unsigned char>& signature);
	bool checkSignRaw(Serializer::pointer data, std::vector<unsigned char>& signature);

	uint32 getAcctSeq() const;
	uint64 getBalance() const;
	void incAcctSeq(uint32 transAcctSeq);

	CKey::pointer getPublicKey();
	CKey::pointer getPrivateKey();

	Json::Value getJson() const;
};

class Wallet
{
protected:
	boost::recursive_mutex mLock;
	std::map<uint160, LocalAccountFamily::pointer> mFamilies;
	std::map<uint160, LocalAccount::pointer> mAccounts;

	LocalAccountFamily::pointer doPrivate(const uint256& passPhrase, bool do_create, bool do_unlock);
	LocalAccountFamily::pointer doPublic(const std::string& pubKey, bool do_create, bool do_db);

	void addFamily(const uint160& family, const std::string& pubKey, int seq,
		const std::string& name, const std::string& comment);

public:
	Wallet() { ; }

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

	static bool isHexPrivateKey(const std::string&);
	static bool isHexPublicKey(const std::string&);
	static bool isHexFamily(const std::string&);
	static std::string privKeyToText(const uint256& privKey);
	static uint256 textToPrivKey(const std::string&);

	static bool unitTest();
};

#endif
