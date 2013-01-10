#ifndef __LEDGER__
#define __LEDGER__

#include <map>
#include <list>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "../json/value.h"

#include "Transaction.h"
#include "TransactionMeta.h"
#include "AccountState.h"
#include "NicknameState.h"
#include "types.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"
#include "InstanceCounter.h"
#include "LoadMonitor.h"

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

#define LEDGER_JSON_DUMP_TXRP	0x10000000
#define LEDGER_JSON_DUMP_STATE	0x20000000
#define LEDGER_JSON_FULL		0x40000000

DEFINE_INSTANCE(Ledger);

class Ledger : public boost::enable_shared_from_this<Ledger>, public IS_INSTANCE(Ledger)
{ // The basic Ledger structure, can be opened, closed, or synching
	friend class TransactionEngine;
	friend class Transactor;
public:
	typedef boost::shared_ptr<Ledger>			pointer;
	typedef const boost::shared_ptr<Ledger>&	ref;

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
		TR_TOOSMALL = 9,	// amount is less than Tx fee
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

	uint32		mReferenceFeeUnits;					// Fee units for the reference transaction
	uint32		mReserveBase, mReserveIncrement;	// Reserve basse and increment in fee units
	uint64		mBaseFee;							// Ripple cost of the reference transaction

	SHAMap::pointer mTransactionMap, mAccountStateMap;

	mutable boost::recursive_mutex mLock;

	static int sPendingSaves;
	static boost::recursive_mutex sPendingSaveLock;

	Ledger(const Ledger&);				// no implementation
	Ledger& operator=(const Ledger&);	// no implementation

protected:
	SLE::pointer getASNode(LedgerStateParms& parms, const uint256& nodeID, LedgerEntryType let);

	static void incPendingSaves();
	static void decPendingSaves();
	void saveAcceptedLedger(bool fromConsensus, LoadEvent::pointer);

	void updateFees();
	void zeroFees();

public:
	Ledger(const RippleAddress& masterID, uint64 startAmount); // used for the starting bootstrap ledger

	Ledger(const uint256 &parentHash, const uint256 &transHash, const uint256 &accountHash,
		uint64 totCoins, uint32 closeTime, uint32 parentCloseTime, int closeFlags, int closeResolution,
		uint32 ledgerSeq); // used for database ledgers

	Ledger(const std::vector<unsigned char>& rawLedger, bool hasPrefix);
	Ledger(const std::string& rawLedger, bool hasPrefix);

	Ledger(bool dummy, Ledger& previous);	// ledger after this one

	Ledger(Ledger& target, bool isMutable); // snapshot

	static Ledger::pointer getSQL(const std::string& sqlStatement);
	static Ledger::pointer getLastFullLedger();
	static int getPendingSaves();

	void updateHash();
	void setClosed()	{ mClosed = true; }
	void setAccepted(uint32 closeTime, int closeResolution, bool correctCloseTime);
	void setAccepted();
	void setImmutable()	{ updateHash(); mImmutable = true; }
	bool isClosed()		{ return mClosed; }
	bool isAccepted()	{ return mAccepted; }
	bool isImmutable()	{ return mImmutable; }

	// ledger signature operations
	void addRaw(Serializer &s) const;
	void setRaw(Serializer& s, bool hasPrefix);

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
	SHAMap::ref peekTransactionMap() { return mTransactionMap; }
	SHAMap::ref peekAccountStateMap() { return mAccountStateMap; }

	// ledger sync functions
	void setAcquiring(void);
	bool isAcquiring(void);
	bool isAcquiringTx(void);
	bool isAcquiringAS(void);

	// Transaction Functions
	bool addTransaction(const uint256& id, const Serializer& txn);
	bool addTransaction(const uint256& id, const Serializer& txn, const Serializer& metaData);
	bool hasTransaction(const uint256& TransID) const { return mTransactionMap->hasItem(TransID); }
	Transaction::pointer getTransaction(const uint256& transID) const;
	bool getTransaction(const uint256& transID, Transaction::pointer& txn, TransactionMetaSet::pointer& txMeta);

	static SerializedTransaction::pointer getSTransaction(SHAMapItem::ref, SHAMapTreeNode::TNType);
	SerializedTransaction::pointer getSMTransaction(SHAMapItem::ref, SHAMapTreeNode::TNType,
	 TransactionMetaSet::pointer& txMeta);

	// high-level functions
	AccountState::pointer getAccountState(const RippleAddress& acctID);
	LedgerStateParms writeBack(LedgerStateParms parms, SLE::ref);
	SLE::pointer getAccountRoot(const uint160& accountID);
	SLE::pointer getAccountRoot(const RippleAddress& naAccountID);
	void updateSkipList();

	// database functions (low-level)
	static Ledger::pointer loadByIndex(uint32 ledgerIndex);
	static Ledger::pointer loadByHash(const uint256& ledgerHash);
	static uint256 getHashByIndex(uint32 index);
	static bool getHashesByIndex(uint32 index, uint256& ledgerHash, uint256& parentHash);
	void pendSave(bool fromConsensus);

	// next/prev function
	SLE::pointer getSLE(const uint256& uHash);
	uint256 getFirstLedgerIndex();
	uint256 getLastLedgerIndex();
	uint256 getNextLedgerIndex(const uint256& uHash);							// first node >hash
	uint256 getNextLedgerIndex(const uint256& uHash, const uint256& uEnd);		// first node >hash, <end
	uint256 getPrevLedgerIndex(const uint256& uHash);							// last node <hash
	uint256 getPrevLedgerIndex(const uint256& uHash, const uint256& uBegin);	// last node <hash, >begin

	// Ledger hash table function
	static uint256 getLedgerHashIndex();
	static uint256 getLedgerHashIndex(uint32 desiredLedgerIndex);
	static int getLedgerHashOffset(uint32 desiredLedgerIndex);
	static int getLedgerHashOffset(uint32 desiredLedgerIndex, uint32 currentLedgerIndex);
	uint256 getLedgerHash(uint32 ledgerIndex);

	static uint256 getLedgerFeatureIndex();
	static uint256 getLedgerFeeIndex();

	// index calculation functions
	static uint256 getAccountRootIndex(const uint160& uAccountID);

	static uint256 getAccountRootIndex(const RippleAddress& account)
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
	static uint256 getDirNodeIndex(const uint256& uDirRoot, const uint64 uNodeIndex = 0);
	static void ownerDirDescriber(SLE::ref, const uint160& owner);

	// Return a node: root or normal
	SLE::pointer getDirNode(LedgerStateParms& parms, const uint256& uNodeIndex);

	//
	// Quality
	//

	static uint256	getQualityIndex(const uint256& uBase, const uint64 uNodeDir = 0);
	static uint256	getQualityNext(const uint256& uBase);
	static uint64	getQuality(const uint256& uBase);
	static void		qualityDirDescriber(SLE::ref,
		const uint160& uTakerPaysCurrency, const uint160& uTakerPaysIssuer,
		const uint160& uTakerGetsCurrency, const uint160& uTakerGetsIssuer,
		const uint64& uRate);

	//
	// Ripple functions : credit lines
	//

	// Index of node which is the ripple state between two accounts for a currency.
	static uint256 getRippleStateIndex(const RippleAddress& naA, const RippleAddress& naB, const uint160& uCurrency);
	static uint256 getRippleStateIndex(const uint160& uiA, const uint160& uiB, const uint160& uCurrency)
		{ return getRippleStateIndex(RippleAddress::createAccountID(uiA), RippleAddress::createAccountID(uiB), uCurrency); }

	SLE::pointer getRippleState(LedgerStateParms& parms, const uint256& uNode);

	SLE::pointer getRippleState(const uint256& uNode)
	{
		LedgerStateParms	qry				= lepNONE;
		return getRippleState(qry, uNode);
	}

	SLE::pointer getRippleState(const RippleAddress& naA, const RippleAddress& naB, const uint160& uCurrency)
		{ return getRippleState(getRippleStateIndex(naA, naB, uCurrency)); }

	SLE::pointer getRippleState(const uint160& uiA, const uint160& uiB, const uint160& uCurrency)
		{ return getRippleState(getRippleStateIndex(RippleAddress::createAccountID(uiA), RippleAddress::createAccountID(uiB), uCurrency)); }

	uint32 getReferenceFeeUnits()
	{
		if (!mBaseFee) updateFees();
		return mReferenceFeeUnits;
	}

	uint64 getBaseFee()
	{
		if (!mBaseFee) updateFees();
		return mBaseFee;
	}

	uint64 getReserve(int increments)
	{
		if (!mBaseFee) updateFees();
		return scaleFeeBase(static_cast<uint64>(increments) * mReserveIncrement + mReserveBase);
	}

	uint64 getReserveInc()
	{
		if (!mBaseFee) updateFees();
		return mReserveIncrement;
	}

	uint64 scaleFeeBase(uint64 fee);
	uint64 scaleFeeLoad(uint64 fee);


	Json::Value getJson(int options);
	void addJson(Json::Value&, int options);

	bool walkLedger();
	bool assertSane();
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
