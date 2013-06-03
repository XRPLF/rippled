#ifndef __LEDGERENTRYSET__
#define __LEDGERENTRYSET__

#include <boost/unordered_map.hpp>

#include "SerializedLedger.h"
#include "TransactionMeta.h"
#include "Ledger.h"
#include "TransactionErr.h"

DEFINE_INSTANCE(LedgerEntrySetEntry);
DEFINE_INSTANCE(LedgerEntrySet);

enum TransactionEngineParams
{
	tapNONE				= 0x00,

	tapNO_CHECK_SIGN	= 0x01,		// Signature already checked

	tapOPEN_LEDGER		= 0x10,		// Transaction is running against an open ledger
		// true = failures are not forwarded, check transaction fee
		// false = debit ledger for consumed funds

	tapRETRY			= 0x20,		// This is not the transaction's last pass
		// Transaction can be retried, soft failures allowed

	tapADMIN			= 0x400,	// Transaction came from a privileged source
};

enum LedgerEntryAction
{
	taaNONE,
	taaCACHED,	// Unmodified.
	taaMODIFY,	// Modifed, must have previously been taaCACHED.
	taaDELETE,	// Delete, must have previously been taaDELETE or taaMODIFY.
	taaCREATE,	// Newly created.
};

class LedgerEntrySetEntry : private IS_INSTANCE(LedgerEntrySetEntry)
{
public:
	SLE::pointer		mEntry;
	LedgerEntryAction	mAction;
	int					mSeq;

	LedgerEntrySetEntry(SLE::ref e, LedgerEntryAction a, int s) : mEntry(e), mAction(a), mSeq(s) { ; }
};


class LedgerEntrySet : private IS_INSTANCE(LedgerEntrySet)
{
public:
	LedgerEntrySet(Ledger::ref ledger, TransactionEngineParams tep, bool immutable = false) :
		mLedger(ledger), mParams(tep), mSeq(0), mImmutable(immutable) { ; }

	LedgerEntrySet() : mParams(tapNONE), mSeq(0), mImmutable(false) { ; }

	// set functions
	void setImmutable()							{ mImmutable = true; }
	bool isImmutable() const					{ return mImmutable; }
	LedgerEntrySet duplicate() const;	// Make a duplicate of this set
	void setTo(const LedgerEntrySet&);	// Set this set to have the same contents as another
	void swapWith(LedgerEntrySet&);		// Swap the contents of two sets
	void invalidate()							{ mLedger.reset(); }
	bool isValid() const						{ return !!mLedger; }

	int getSeq() const							{ return mSeq; }
	TransactionEngineParams getParams() const	{ return mParams; }
	void bumpSeq()								{ ++mSeq; }
	void init(Ledger::ref ledger, const uint256& transactionID, uint32 ledgerID, TransactionEngineParams params);
	void clear();

	Ledger::pointer& getLedger()		{ return mLedger; }
	Ledger::ref getLedgerRef() const	{ return mLedger; }

	// basic entry functions
	SLE::pointer getEntry(const uint256& index, LedgerEntryAction&);
	LedgerEntryAction hasEntry(const uint256& index) const;
	void entryCache(SLE::ref);		// Add this entry to the cache
	void entryCreate(SLE::ref);		// This entry will be created
	void entryDelete(SLE::ref);		// This entry will be deleted
	void entryModify(SLE::ref);		// This entry will be modified
	bool hasChanges();				// True if LES has any changes

	// higher-level ledger functions
	SLE::pointer entryCreate(LedgerEntryType letType, const uint256& uIndex);
	SLE::pointer entryCache(LedgerEntryType letType, const uint256& uIndex);

	// Directory functions.
	TER dirAdd(
		uint64&								uNodeDir,		// Node of entry.
		const uint256&						uRootIndex,
		const uint256&						uLedgerIndex,
		FUNCTION_TYPE<void (SLE::ref)>		fDescriber);

	TER dirDelete(
		const bool						bKeepRoot,
		const uint64&					uNodeDir,		// Node item is mentioned in.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex,	// Item being deleted
		const bool						bStable,
		const bool						bSoft);

	bool				dirFirst(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);
	bool				dirNext(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);
	TER					dirCount(const uint256& uDirIndex, uint32& uCount);

	uint256				getNextLedgerIndex(const uint256& uHash);
	uint256				getNextLedgerIndex(const uint256& uHash, const uint256& uEnd);

	void				ownerCountAdjust(const uint160& uOwnerID, int iAmount, SLE::ref sleAccountRoot=SLE::pointer());

	// Offer functions.
	TER					offerDelete(const uint256& uOfferIndex);
	TER					offerDelete(SLE::ref sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID);

	// Balance functions.
	uint32				rippleTransferRate(const uint160& uIssuerID);
	uint32				rippleTransferRate(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID);
	STAmount			rippleOwed(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);
	STAmount			rippleLimit(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);
	uint32				rippleQualityIn(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID,
		SField::ref sfLow = sfLowQualityIn, SField::ref sfHigh = sfHighQualityIn);
	uint32				rippleQualityOut(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
	{ return rippleQualityIn(uToAccountID, uFromAccountID, uCurrencyID, sfLowQualityOut, sfHighQualityOut); }

	STAmount			rippleHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
	STAmount			rippleTransferFee(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID, const STAmount& saAmount);
	TER					rippleCredit(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, bool bCheckIssuer=true);
	TER					rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, STAmount& saActual);

	STAmount			accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
	STAmount			accountFunds(const uint160& uAccountID, const STAmount& saDefault);
	TER					accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount);

	TER					trustCreate(
							const bool		bSrcHigh,
							const uint160&	uSrcAccountID,
							const uint160&	uDstAccountID,
							const uint256&	uIndex,
							SLE::ref		sleAccount,
							const bool		bAuth,
							const STAmount& saSrcBalance,
							const STAmount& saSrcLimit,
							const uint32	uSrcQualityIn = 0,
							const uint32	uSrcQualityOut = 0);
	TER					trustDelete(SLE::ref sleRippleState, const uint160& uLowAccountID, const uint160& uHighAccountID);

	Json::Value getJson(int) const;
	void calcRawMeta(Serializer&, TER result, uint32 index);

	// iterator functions
	typedef std::map<uint256, LedgerEntrySetEntry>::iterator				iterator;
	typedef std::map<uint256, LedgerEntrySetEntry>::const_iterator			const_iterator;
	bool isEmpty() const													{ return mEntries.empty(); }
	std::map<uint256, LedgerEntrySetEntry>::const_iterator begin() const	{ return mEntries.begin(); }
	std::map<uint256, LedgerEntrySetEntry>::const_iterator end() const		{ return mEntries.end(); }
	std::map<uint256, LedgerEntrySetEntry>::iterator begin()				{ return mEntries.begin(); }
	std::map<uint256, LedgerEntrySetEntry>::iterator end()					{ return mEntries.end(); }

	static bool intersect(const LedgerEntrySet& lesLeft, const LedgerEntrySet& lesRight);

private:
	Ledger::pointer mLedger;
	std::map<uint256, LedgerEntrySetEntry>	mEntries; // cannot be unordered!
	TransactionMetaSet mSet;
	TransactionEngineParams mParams;
	int mSeq;
	bool mImmutable;

	LedgerEntrySet(Ledger::ref ledger, const std::map<uint256, LedgerEntrySetEntry> &e,
		const TransactionMetaSet& s, int m) :
			mLedger(ledger), mEntries(e), mSet(s), mParams(tapNONE), mSeq(m), mImmutable(false) { ; }

	SLE::pointer getForMod(const uint256& node, Ledger::ref ledger,
		boost::unordered_map<uint256, SLE::pointer>& newMods);

	bool threadTx(const RippleAddress& threadTo, Ledger::ref ledger,
		boost::unordered_map<uint256, SLE::pointer>& newMods);

	bool threadTx(SLE::ref threadTo, Ledger::ref ledger, boost::unordered_map<uint256, SLE::pointer>& newMods);

	bool threadOwners(SLE::ref node, Ledger::ref ledger, boost::unordered_map<uint256, SLE::pointer>& newMods);
};

inline LedgerEntrySet::iterator range_begin(LedgerEntrySet& x)		{ return x.begin(); }
inline LedgerEntrySet::iterator range_end(LedgerEntrySet &x)		{ return x.end(); }
namespace boost
{
	template<> struct range_mutable_iterator<LedgerEntrySet>	{ typedef LedgerEntrySet::iterator type; };
	template<> struct range_const_iterator<LedgerEntrySet>		{ typedef LedgerEntrySet::const_iterator type; };
}

#endif
// vim:ts=4
