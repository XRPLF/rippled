#ifndef __LOCALACCOUNT__
#define __LOCALACCOUNT__

#include "boost/enable_shared_from_this.hpp"
#include "boost/shared_ptr.hpp"

class LocalAccountFamily;

class LocalAccount
{ // tracks keys for local accounts
public:
	typedef boost::shared_ptr<LocalAccount> pointer;

protected:
	// core account information
	CKey::pointer mPublicKey;
	uint160 mAcctID;
	std::string mName, mComment;

	// family information
	boost::shared_ptr<LocalAccountFamily> mFamily;
	int mAccountFSeq;

	// local usage tracking
	uint64 mLgrBalance;		// The balance, from the last ledger
	int64 mTxnDelta;		// The balance changes from local/pending transactions
	uint32 mTxnSeq;			// The sequence number of the next transaction

public:
	LocalAccount(boost::shared_ptr<LocalAccountFamily> family, int accountSeq);

	// Database operations
	bool read();			// reads any existing data
	bool write();			// creates the record in the first place
	bool updateName();		// writes changed name/comment
	bool updateBalance();	// writes changed balance/seq

	const uint160& getAddress() const { return mAcctID; }
	int getAcctFSeq() const { return mAccountFSeq; }

	std::string getLocalAccountName() const;			// The name used locally to identify this account
	std::string getAccountName() const;					// The normal account name used to send to this account
	std::string getFullName() const;
	std::string getShortName() const;
	std::string getFamilyName() const;

	bool isLocked() const;
	bool isIssued() const;

	CKey::pointer getPublicKey() { return mPublicKey; }
	CKey::pointer getPrivateKey();
	Json::Value getJson() const;

	void update(uint64 balance, uint32 seq);
	uint32 getTxnSeq() const { return  mTxnSeq; }
	uint32 incTxnSeq() { return mTxnSeq++; }

	int64 getEffectiveBalance() const { return static_cast<int64_t>(mLgrBalance)+mTxnDelta; }
	void credit(uint64 amount) { mTxnDelta+=amount; }
	void debit(uint64 amount) { mTxnDelta-=amount; }
	void setLedgerBalance(uint64_t lb) { mLgrBalance=lb; if(mTxnSeq==0) mTxnSeq=1; }

	void syncLedger();
};

class LocalAccountFamily : public boost::enable_shared_from_this<LocalAccountFamily>
{ // tracks families of local accounts
public:
	typedef boost::shared_ptr<LocalAccountFamily> pointer;

protected:
	std::map<int, LocalAccount::pointer> mAccounts;

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

	std::map<int, LocalAccount::pointer>& getAcctMap() { return mAccounts; }
	LocalAccount::pointer get(int seq);
	uint160 getAccount(int seq, bool keep);
	CKey::pointer getPrivateKey(int seq);
	CKey::pointer getPublicKey(int seq);

	std::string getPubGenHex() const;	// The text name of the public key
	std::string getShortName() const { return mName; }
	std::string getComment() const { return mComment; }
	Json::Value getJson() const;

	static std::string getSQLFields();
	std::string getSQL() const;
	static LocalAccountFamily::pointer readFamily(const uint160& family);
	void write(bool is_new);
};

#endif
