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

	void unlock(BIGNUM* rootPrivKey);
	void lock() { mPrivateKey=CKey::pointer(); }

	void write();
	void read();

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
	EC_POINT*	rootPubKey;

	BIGNUM*		rootPrivateKey;

public:

	LocalAccountFamily(const uint160& family, EC_POINT* pubKey);
	~LocalAccountFamily();

	const uint160& getFamily() { return mFamily; }
	bool isLocked() const { return rootPrivateKey==NULL; }
	void unlock(BIGNUM* privateKey);
	void lock();

	const EC_POINT* peekPubKey() const { return rootPubKey; }

	LocalAccountEntry::pointer get(int seq);
	uint160 getAccount(int seq);

	std::string getPubName() const;		// The text name of the public key
	std::string getShortName() const;	// The text name for the family
};

class LocalAccount
{ // tracks a single local account
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

	uint160 doPrivate(const uint256& passPhrase, bool create, bool unlock);

public:
	Wallet() { ; }

	uint160 addFamily(const std::string& passPhrase, bool lock);
	uint160 addFamily(const uint256& passPhrase, bool lock) { return doPrivate(passPhrase, true, !lock); }
	bool addFamily(const uint160& familyName, const std::string& pubKey);

	uint160 unlock(const uint256& passPhrase) { return doPrivate(passPhrase, false, true); }

	bool lock(const uint160& familyName);

	void load(void);

	LocalAccount::pointer getLocalAccount(const uint160& famBase, int seq);
	LocalAccount::pointer getLocalAccount(const uint160& acctID);
	std::string getPubKey(const uint160& famBase);
};

#endif
