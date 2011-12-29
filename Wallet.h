#ifndef __WALLET__
#define __WALLET__

#include <vector>
#include <map>
#include <string>

#include <boost/shared_ptr.hpp>

#include "openssl/ec.h"

#include "uint256.h"
#include "Serializer.h"

class LocalAccountEntry
{ // tracks keys for local accounts
public:
	typedef boost::shared_ptr<LocalAccountEntry> pointer;

protected:
	// family informaiton
	uint160 mAccountFamily;
	int mAccountSeq;

	// core account information
	CKey::pointer mPublicKey;
	CKey::pointer mPrivateKey;
	uint160 mAcctID;

	// local usage tracking
	uint64 mBalance;		// The balance, last we checked/updated
	uint32 mLedgerSeq;		// The ledger seq when we updated the balance
	uint32 mTxnSeq;			// The sequence number of the next transaction

public:
	LocalAccountEntry(const uint160& accountFamily, int accountSeq, EC_POINT* rootPubKey);

	void unlock(const BIGNUM* rootPrivKey);
	void lock() { mPrivateKey=CKey::pointer(); }

	bool write(bool create);
	bool read();

	const uint160& getAccountID() const { return mAcctID; }
	int getAccountSeq() const { return mAccountSeq; }
	std::string getLocalAccountName() const;			// The name used locally to identify this account
	std::string getAccountName() const;					// The normal account name used to send to this account

	CKey::pointer getPubKey() { return mPublicKey; }
	CKey::pointer getPrivKey() { return mPrivateKey; }

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

	const uint160& getFamily() { return mFamily; }
	bool isLocked() const { return mRootPrivateKey==NULL; }
	void unlock(const BIGNUM* privateKey);
	void lock();

	void setSeq(uint32 s) { mLastSeq=s; }
	void setName(const std::string& n) { mName=n; }
	void setComment(const std::string& c) { mComment=c; }

	std::map<int, LocalAccountEntry::pointer>& getAcctMap() { return mAccounts; }
	LocalAccountEntry::pointer get(int seq);
	uint160 getAccount(int seq);

	std::string getPubKeyHex() const;	// The text name of the public key
	std::string getShortName() const { return mName; }

	static std::string getSQLFields();
	std::string getSQL() const;

	static bool isHexPrivateKey(const std::string&);
	static bool isHexPublicKey(const std::string&);
};

class LocalAccount
{ // tracks a single local account in a form that can be passed to other code.
  // We can't hand other code a LocalAccountEntry because an orphaned entry
  // could hold a private key we can't lock.
public:
	typedef boost::shared_ptr<LocalAccount> pointer;

protected:
	LocalAccountFamily::pointer mFamily;
	int mSeq;

public:
	LocalAccount(LocalAccountFamily::pointer fam, int seq) : mFamily(fam), mSeq(seq) { ; }
	uint160 getAddress() const;
	bool isLocked() const;

	bool signRaw(Serializer::pointer);
	bool signRaw(Serializer::pointer, std::vector<unsigned char>& signature);
	bool checkSignRaw(Serializer::pointer data, std::vector<unsigned char>& signature);

	uint32 getAcctSeq() const;
	uint64 getBalance() const;
	void incAcctSeq(uint32 transAcctSeq);

	CKey::pointer getPublicKey();
	CKey::pointer getPrivateKey();
};

class Wallet
{
protected:
	std::map<uint160, LocalAccountFamily::pointer> families;
	std::map<uint160, LocalAccount::pointer> accounts;

	LocalAccountFamily::pointer doPrivate(const uint256& passPhrase, bool do_create, bool do_unlock);
	LocalAccountFamily::pointer doPublic(const std::string& pubKey);

	void addFamily(const uint160& family, const std::string& pubKey, int seq,
		const std::string& name, const std::string& comment);

public:
	Wallet() { ; }

	uint160 addFamily(const std::string& passPhrase, bool lock);
	uint160 addFamily(const uint256& passPhrase, bool lock);
	uint160 addFamily(const std::string& pubKey);
	uint160 addFamily(uint256& privKey);

	void delFamily(const uint160& familyName);

	uint160 unlock(const uint256& passPhrase);
	bool lock(const uint160& familyName);

	void load(void);

	LocalAccount::pointer getLocalAccount(const uint160& famBase, int seq);
	LocalAccount::pointer getLocalAccount(const uint160& acctID);
	std::string getPubKeyHex(const uint160& famBase);
	std::string getShortName(const uint160& famBase);

	static bool unitTest();
};

#endif
