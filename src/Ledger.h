#ifndef __LEDGER__
#define __LEDGER__

#include <map>
#include <list>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "../json/value.h"

#include "Transaction.h"
#include "AccountState.h"
#include "RippleState.h"
#include "NicknameState.h"
#include "types.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

enum LedgerStateParms
{
	lepNONE			= 0,	// no special flags

	// input flags
	lepCREATE		= 1,	// Create if not present

	// output flags
	lepOKAY			= 2,	// success
	lepMISSING		= 4,	// No node in that slot
	lepWRONGTYPE	= 8,	// Node of different type there
	lepCREATED		= 16,	// Node was created
	lepERROR		= 32,	// error
};

#define LEDGER_JSON_DUMP_TXNS	0x10000000
#define LEDGER_JSON_DUMP_STATE	0x20000000
#define LEDGER_JSON_FULL		0x40000000

class Ledger : public boost::enable_shared_from_this<Ledger>
{ // The basic Ledger structure, can be opened, closed, or synching
	friend class TransactionEngine;
public:
	typedef boost::shared_ptr<Ledger> pointer;

	enum TransResult
	{
		TR_ERROR	= -1,
		TR_SUCCESS	= 0,
		TR_NOTFOUND	= 1,
		TR_ALREADY	= 2,
		TR_BADTRANS = 3,	// the transaction itself is corrupt
		TR_BADACCT	= 4,	// one of the accounts is invalid
		TR_INSUFF	= 5,	// the sending(apply)/receiving(remove) account is broke
		TR_PASTASEQ	= 6,	// account is past this transaction
		TR_PREASEQ	= 7,	// account is missing transactions before this
		TR_BADLSEQ	= 8,	// ledger too early
		TR_TOOSMALL = 9, // amount is less than Tx fee
	};


private:
	static uint64 sGenesisClose;

	uint256		mHash, mParentHash, mTransHash, mAccountHash;
	uint64		mTotCoins;
	uint64		mCloseTime; // when this ledger should close / did close
	uint64		mPrevClose; // when the previous ledger closed
	uint32		mLedgerSeq;
	bool		mClosed, mValidHash, mAccepted, mImmutable;

	SHAMap::pointer mTransactionMap, mAccountStateMap;

	mutable boost::recursive_mutex mLock;

	Ledger(const Ledger&);				// no implementation
	Ledger& operator=(const Ledger&);	// no implementation

protected:

	bool addTransaction(Transaction::pointer);
	bool addTransaction(const uint256& id, const Serializer& txn);

	static Ledger::pointer getSQL(const std::string& sqlStatement);

	SLE::pointer getASNode(LedgerStateParms& parms, const uint256& nodeID, LedgerEntryType let);

public:
	Ledger(const NewcoinAddress& masterID, uint64 startAmount); // used for the starting bootstrap ledger
	Ledger(const uint256 &parentHash, const uint256 &transHash, const uint256 &accountHash,
		uint64 totCoins, uint64 timeStamp, uint32 ledgerSeq); // used for database ledgers
	Ledger(const std::vector<unsigned char>& rawLedger);
	Ledger(const std::string& rawLedger);
	Ledger(uint64 defaultCloseTime, Ledger& previous);	// ledger after this one
	Ledger(Ledger& target, bool isMutable); // snapshot

	void updateHash();
	void setClosed()	{ mClosed = true; }
	void setAccepted()	{ mAccepted = true; }
	void setImmutable()	{ updateHash(); mImmutable = true; }
	bool isClosed()		{ return mClosed; }
	bool isAccepted()	{ return mAccepted; }
	bool isImmutable()	{ return mImmutable; }
	void armDirty()		{ mTransactionMap->armDirty();		mAccountStateMap->armDirty(); }
	void disarmDirty()	{ mTransactionMap->disarmDirty();	mAccountStateMap->disarmDirty(); }

	// This ledger has closed, will never be accepted, and is accepting
	// new transactions to be re-repocessed when do accept a new last-closed ledger
	void bumpSeq()		{ mClosed = true; mLedgerSeq++; }

	// ledger signature operations
	void addRaw(Serializer &s);

	uint256 getHash();
	const uint256& getParentHash() const	{ return mParentHash; }
	const uint256& getTransHash() const		{ return mTransHash; }
	const uint256& getAccountHash() const	{ return mAccountHash; }
	uint64 getTotalCoins() const			{ return mTotCoins; }
	void destroyCoins(uint64 fee)			{ mTotCoins -= fee; }
	uint64 getCloseTimeNC() const			{ return mCloseTime; }
	uint32 getLedgerSeq() const				{ return mLedgerSeq; }

	// close time functions
	void setCloseTime(uint64 ct)			{ assert(!mImmutable); mCloseTime = ct; }
	void setCloseTime(boost::posix_time::ptime);
	boost::posix_time::ptime getCloseTime() const;
	uint64 getNextLedgerClose(int prevCloseSeconds) const;

	// low level functions
	SHAMap::pointer peekTransactionMap() { return mTransactionMap; }
	SHAMap::pointer peekAccountStateMap() { return mAccountStateMap; }
	Ledger::pointer snapShot(bool isMutable);

	// ledger sync functions
	void setAcquiring(void);
	bool isAcquiring(void);
	bool isAcquiringTx(void);
	bool isAcquiringAS(void);

	// Transaction Functions
	bool hasTransaction(const uint256& TransID) const;
	Transaction::pointer getTransaction(const uint256& transID) const;

	// high-level functions
	AccountState::pointer getAccountState(const NewcoinAddress& acctID);
	LedgerStateParms writeBack(LedgerStateParms parms, SLE::pointer);
	SLE::pointer getAccountRoot(LedgerStateParms& parms, const uint160& accountID);
	SLE::pointer getAccountRoot(LedgerStateParms& parms, const NewcoinAddress& naAccountID);

	// database functions
	static void saveAcceptedLedger(Ledger::pointer);
	static Ledger::pointer loadByIndex(uint32 ledgerIndex);
	static Ledger::pointer loadByHash(const uint256& ledgerHash);

	// next/prev function
	SLE::pointer getNextSLE(const uint256& hash);						// first node >hash
	SLE::pointer getNextSLE(const uint256& hash, const uint256& max); 	// first node >hash, <max
	SLE::pointer getPrevSLE(const uint256& hash);						// last node <hash
	SLE::pointer getPrevSLE(const uint256& hash, const uint256& min);	// last node <hash, >min

	// index calculation functions
	static uint256 getAccountRootIndex(const uint160& uAccountID);

	static uint256 getAccountRootIndex(const NewcoinAddress& account)
	{ return getAccountRootIndex(account.getAccountID()); }

	//
	// Generator Map functions
	//

	SLE::pointer getGenerator(LedgerStateParms& parms, const uint160& uGeneratorID);

	static uint256 getGeneratorIndex(const uint160& uGeneratorID);

	//
	// Nickname functions
	//

	static uint256 getNicknameHash(const std::string& strNickname)
	{ Serializer s(strNickname); return s.getSHA256(); }

	NicknameState::pointer getNicknameState(const uint256& uNickname);
	NicknameState::pointer getNicknameState(const std::string& strNickname)
	{ return getNicknameState(getNicknameHash(strNickname)); }

	SLE::pointer getNickname(LedgerStateParms& parms, const uint256& uNickname);
	SLE::pointer getNickname(LedgerStateParms& parms, const std::string& strNickname)
	{ return getNickname(parms, getNicknameHash(strNickname)); }

	static uint256 getNicknameIndex(const uint256& uNickname);

	//
	// Offer functions
	//

	static uint160 getOfferBase(const uint160& currencyIn, const uint160& accountIn,
		const uint160& currencyOut, const uint160& accountOut);

	static uint256 getOfferIndex(const uint160& offerBase, uint64 rate, int skip = 0);

	static int getOfferSkip(const uint256& offerId);

	//
	// Directory functions
	//

	static uint256 getDirIndex(const uint256& uBase, const uint64 uNodeDir=0);

	SLE::pointer getDirRoot(LedgerStateParms& parms, const uint256& uRootIndex);
	SLE::pointer getDirNode(LedgerStateParms& parms, const uint256& uNodeIndex);

	//
	// Ripple functions
	//

	static uint256 getRippleStateIndex(const NewcoinAddress& naA, const NewcoinAddress& naB, const uint160& uCurrency);
	static uint256 getRippleStateIndex(const uint160& uiA, const uint160& uiB, const uint160& uCurrency)
		{ return getRippleStateIndex(NewcoinAddress::createAccountID(uiA), NewcoinAddress::createAccountID(uiB), uCurrency); }

	static uint256 getRippleStateIndex(const NewcoinAddress& naA, const NewcoinAddress& naB)
		{ return getRippleStateIndex(naA, naB, uint160()); }
	static uint256 getRippleDirIndex(const uint160& uAccountID);

	RippleState::pointer getRippleState(const uint256& uNode);
	SLE::pointer getRippleState(LedgerStateParms& parms, const uint256& uNode);

	SLE::pointer getRippleState(LedgerStateParms& parms, const NewcoinAddress& naA, const NewcoinAddress& naB, const uint160& uCurrency)
		{ return getRippleState(parms, getRippleStateIndex(naA, naB, uCurrency)); }

	SLE::pointer getRippleState(LedgerStateParms& parms, const uint160& uiA, const uint160& uiB, const uint160& uCurrency)
		{ return getRippleState(parms, getRippleStateIndex(NewcoinAddress::createAccountID(uiA), NewcoinAddress::createAccountID(uiB), uCurrency)); }

	//
	// Misc
	//
	bool isCompatible(boost::shared_ptr<Ledger> other);
//	bool signLedger(std::vector<unsigned char> &signature, const LocalHanko &hanko);

	void addJson(Json::Value&, int options);

	static bool unitTest();
};

inline LedgerStateParms operator|(const LedgerStateParms& l1, const LedgerStateParms& l2)
{
	return static_cast<LedgerStateParms>(static_cast<int>(l1) | static_cast<int>(l2));
}

inline LedgerStateParms operator&(const LedgerStateParms& l1, const LedgerStateParms& l2)
{
	return static_cast<LedgerStateParms>(static_cast<int>(l1) & static_cast<int>(l2));
}

#endif
// vim:ts=4
