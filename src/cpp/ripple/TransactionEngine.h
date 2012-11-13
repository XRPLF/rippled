#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include "Ledger.h"
#include "SerializedTransaction.h"
#include "SerializedLedger.h"
#include "LedgerEntrySet.h"
#include "TransactionErr.h"
#include "InstanceCounter.h"

DEFINE_INSTANCE(TransactionEngine);

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TransactionEngineParams
{
	tapNONE				= 0x00,

	tapNO_CHECK_SIGN	= 0x01,	// Signature already checked

	tapOPEN_LEDGER		= 0x10,	// Transaction is running against an open ledger
		// true = failures are not forwarded, check transaction fee
		// false = debit ledger for consumed funds

	tapRETRY			= 0x20,	// This is not the transaction's last pass
		// Transaction can be retried, soft failures allowed
};

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine : private IS_INSTANCE(TransactionEngine)
{
private:
	LedgerEntrySet						mNodes;

	TER	setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator);

	TER takeOffers(
		bool				bPassive,
		const uint256&		uBookBase,
		const uint160&		uTakerAccountID,
		const SLE::pointer&	sleTakerAccount,
		const STAmount&		saTakerPays,
		const STAmount&		saTakerGets,
		STAmount&			saTakerPaid,
		STAmount&			saTakerGot);

protected:
	Ledger::pointer		mLedger;

	uint160				mTxnAccountID;
	SLE::pointer		mTxnAccount;

	SLE::pointer		entryCreate(LedgerEntryType type, const uint256& index)		{ return mNodes.entryCreate(type, index); }
	SLE::pointer		entryCache(LedgerEntryType type, const uint256& index)		{ return mNodes.entryCache(type, index); }
	void				entryDelete(SLE::ref sleEntry)								{ mNodes.entryDelete(sleEntry); }
	void				entryModify(SLE::ref sleEntry)								{ mNodes.entryModify(sleEntry); }

	void				txnWrite();

	TER					doAccountSet(const SerializedTransaction& txn);
	TER					doTrustSet(const SerializedTransaction& txn);
	TER					doOfferCreate(const SerializedTransaction& txn);
	TER					doOfferCancel(const SerializedTransaction& txn);
	TER					doRegularKeySet(const SerializedTransaction& txn);
	TER					doPayment(const SerializedTransaction& txn, const TransactionEngineParams params);
	TER					doWalletAdd(const SerializedTransaction& txn);
	TER					doContractAdd(const SerializedTransaction& txn);
	TER					doContractRemove(const SerializedTransaction& txn);

public:
	TransactionEngine() { ; }
	TransactionEngine(Ledger::ref ledger) : mLedger(ledger) { assert(mLedger); }

	Ledger::pointer getLedger()			{ return mLedger; }
	void setLedger(Ledger::ref ledger)	{ assert(ledger); mLedger = ledger; }

	TER applyTransaction(const SerializedTransaction&, TransactionEngineParams);
};

inline TransactionEngineParams operator|(const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
	return static_cast<TransactionEngineParams>(static_cast<int>(l1) | static_cast<int>(l2));
}

inline TransactionEngineParams operator&(const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
	return static_cast<TransactionEngineParams>(static_cast<int>(l1) & static_cast<int>(l2));
}

#endif
// vim:ts=4
