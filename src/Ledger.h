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

	// ledger close flags
	static const uint32 sLCF_NoConsensusTime = 1;

private:

	uint256		mHash, mParentHash, mTransHash, mAccountHash;
	uint64		mTotCoins;
	uint32		mLedgerSeq;
	uint32		mCloseTime;			// when this ledger closed
	uint32		mParentCloseTime;	// when the previous ledger closed
	int			mCloseResolution;	// the resolution for this ledger close time (2-120 seconds)
	uint32		mCloseFlags;		// flags indicating how this ledger close took place
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
		uint64 totCoins, uint32 closeTime, uint32 parentCloseTime, int closeFlags, int closeResolution,
		uint32 ledgerSeq); // used for database ledgers

	Ledger(const std::vector<unsigned char>& rawLedger);

	Ledger(const std::string& rawLedger);

	Ledger(bool dummy, Ledger& previous);	// ledger after this one

	Ledger(Ledger& target, bool isMutable); // snapshot

	void updateHash();
	void setClosed()	{ mClosed = true; }
	void setAccepted(uint32 closeTime, int closeResolution, bool correctCloseTime);
	void setAccepted();
	void setImmutable()	{ updateHash(); mImmutable = true; }
	bool isClosed()		{ return mClosed; }
	bool isAccepted()	{ return mAccepted; }
	bool isImmutable()	{ return mImmutable; }
	void armDirty()		{ mTransactionMap->armDirty();		mAccountStateMap->armDirty(); }
	void disarmDirty()	{ mTransactionMap->disarmDirty();	mAccountStateMap->disarmDirty(); }

	// This ledger has closed, will never be accepted, and is accepting
	// new transactions to be re-reprocessed when do accept a new last-closed ledger
	void bumpSeq()		{ mClosed = true; mLedgerSeq++; }

	// ledger signature operations
	void addRaw(Serializer &s) const;
	void setRaw(const Serializer& s);

	uint256 getHash();
	const uint256& getParentHash() const	{ return mParentHash; }
	const uint256& getTransHash() const		{ return mTransHash; }
	const uint256& getAccountHash() const	{ return mAccountHash; }
	uint64 getTotalCoins() const			{ return mTotCoins; }
	void destroyCoins(uint64 fee)			{ mTotCoins -= fee; }
	uint32 getCloseTimeNC() const			{ return mCloseTime; }
	uint32 getParentCloseTimeNC() const		{ return mParentCloseTime; }
	uint32 getLedgerSeq() const				{ return mLedgerSeq; }
	int getCloseResolution() const			{ return mCloseResolution; }
	bool getCloseAgree() const				{ return (mCloseFlags & sLCF_NoConsensusTime) == 0; }

	// close time functions
	void setCloseTime(uint32 ct)			{ assert(!mImmutable); mCloseTime = ct; }
	void setCloseTime(boost::posix_time::ptime);
	boost::posix_time::ptime getCloseTime() const;

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
	bool hasTransaction(const uint256& TransID) const { return mTransactionMap->hasItem(TransID); }
	Transaction::pointer getTransaction(const uint256& transID) const;

	// high-level functions
	AccountState::pointer getAccountState(const NewcoinAddress& acctID);
	LedgerStateParms writeBack(LedgerStateParms parms, SLE::pointer);
	SLE::pointer getAccountRoot(const uint160& accountID);
	SLE::pointer getAccountRoot(const NewcoinAddress& naAccountID);

	// database functions
	static void saveAcceptedLedger(Ledger::pointer);
	static Ledger::pointer loadByIndex(uint32 ledgerIndex);
	static Ledger::pointer loadByHash(const uint256& ledgerHash);

	// next/prev function
	SLE::pointer getSLE(const uint256& uHash);
	uint256 getFirstLedgerIndex();
	uint256 getLastLedgerIndex();
	uint256 getNextLedgerIndex(const uint256& uHash);							// first node >hash
	uint256 getNextLedgerIndex(const uint256& uHash, const uint256& uEnd);		// first node >hash, <end
	uint256 getPrevLedgerIndex(const uint256& uHash);							// last node <hash
	uint256 getPrevLedgerIndex(const uint256& uHash, const uint256& uBegin);	// last node <hash, >begin

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
	// Order book functions
	//

	// Order book dirs have a base so we can use next to step through them in quality order.
	static uint256 getBookBase(const uint160& uTakerPaysCurrency, const uint160& uTakerPaysIssuerID,
		const uint160& uTakerGetsCurrency, const uint160& uTakerGetsIssuerID);

	//
	// Offer functions
	//

	SLE::pointer getOffer(LedgerStateParms& parms, const uint256& uIndex);

	SLE::pointer getOffer(const uint256& uIndex)
	{
		LedgerStateParms	qry				= lepNONE;
		return getOffer(qry, uIndex);
	}

	SLE::pointer getOffer(LedgerStateParms& parms, const uint160& uAccountID, uint32 uSequence)
	{ return getOffer(parms, getOfferIndex(uAccountID, uSequence)); }

	// The index of an offer.
	static uint256 getOfferIndex(const uint160& uAccountID, uint32 uSequence);

	//
	// Owner functions
	//

	// All items controlled by an account are here: offers
	static uint256 getOwnerDirIndex(const uint160& uAccountID);

	//
	// Directory functions
	// Directories are doubly linked lists of nodes.

	// Given a directory root and and index compute the index of a node.
	static uint256 getDirNodeIndex(const uint256& uDirRoot, const uint64 uNodeIndex=0);

	// Return a node: root or normal
	SLE::pointer getDirNode(LedgerStateParms& parms, const uint256& uNodeIndex);

	//
	// Quality
	//

	static uint256	getQualityIndex(const uint256& uBase, const uint64 uNodeDir=0);
	static uint256	getQualityNext(const uint256& uBase);
	static uint64	getQuality(const uint256& uBase);

	//
	// Ripple functions : credit lines
	//

	// Index of node which is the ripple state between two accounts for a currency.
	static uint256 getRippleStateIndex(const NewcoinAddress& naA, const NewcoinAddress& naB, const uint160& uCurrency);
	static uint256 getRippleStateIndex(const uint160& uiA, const uint160& uiB, const uint160& uCurrency)
		{ return getRippleStateIndex(NewcoinAddress::createAccountID(uiA), NewcoinAddress::createAccountID(uiB), uCurrency); }

	RippleState::pointer accessRippleState(const uint256& uNode);

	SLE::pointer getRippleState(LedgerStateParms& parms, const uint256& uNode);

	SLE::pointer getRippleState(const uint256& uNode)
		{
			LedgerStateParms	qry				= lepNONE;
			return getRippleState(qry, uNode);
		}

	SLE::pointer getRippleState(const NewcoinAddress& naA, const NewcoinAddress& naB, const uint160& uCurrency)
		{ return getRippleState(getRippleStateIndex(naA, naB, uCurrency)); }

	SLE::pointer getRippleState(const uint160& uiA, const uint160& uiB, const uint160& uCurrency)
		{ return getRippleState(getRippleStateIndex(NewcoinAddress::createAccountID(uiA), NewcoinAddress::createAccountID(uiB), uCurrency)); }

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
