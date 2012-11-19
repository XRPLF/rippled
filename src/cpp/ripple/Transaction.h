#ifndef __TRANSACTION__
#define __TRANSACTION__

//
// Transactions should be constructed in JSON with. Use STObject::parseJson to obtain a binary version.
//

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/cstdint.hpp>

#include "../json/value.h"

#include "key.h"
#include "uint256.h"
#include "ripple.pb.h"
#include "Serializer.h"
#include "SHAMap.h"
#include "SerializedTransaction.h"
#include "TransactionErr.h"
#include "InstanceCounter.h"

class Database;

enum TransStatus
{
	NEW			= 0, // just received / generated
	INVALID		= 1, // no valid signature, insufficient funds
	INCLUDED	= 2, // added to the current ledger
	CONFLICTED	= 3, // losing to a conflicting transaction
	COMMITTED	= 4, // known to be in a ledger
	HELD		= 5, // not valid now, maybe later
	REMOVED		= 6, // taken out of a ledger
	OBSOLETE	= 7, // a compatible transaction has taken precedence
	INCOMPLETE	= 8  // needs more signatures
};

DEFINE_INSTANCE(Transaction);

// This class is for constructing and examining transactions.  Transactions are static so manipulation functions are unnecessary.
class Transaction : public boost::enable_shared_from_this<Transaction>, private IS_INSTANCE(Transaction)
{
public:
	typedef boost::shared_ptr<Transaction> pointer;

private:
	uint256			mTransactionID;
	RippleAddress	mAccountFrom;
	RippleAddress	mFromPubKey;	// Sign transaction with this. mSignPubKey
	RippleAddress	mSourcePrivate;	// Sign transaction with this.

	uint32			mInLedger;
	TransStatus		mStatus;
	TER				mResult;

	SerializedTransaction::pointer mTransaction;

public:
	Transaction(SerializedTransaction::ref st, bool bValidate);

	static Transaction::pointer sharedTransaction(const std::vector<unsigned char>&vucTransaction, bool bValidate);
	static Transaction::pointer transactionFromSQL(Database* db, bool bValidate);

	Transaction(
		TransactionType ttKind,
		const RippleAddress&	naPublicKey,		// To prove transaction is consistent and authorized.
		const RippleAddress&	naSourceAccount,	// To identify the paying account.
		uint32					uSeq,				// To order transactions.
		const STAmount&			saFee,				// Transaction fee.
		uint32					uSourceTag);		// User call back value.


	bool sign(const RippleAddress& naAccountPrivate);
	bool checkSign() const;
	void updateID() { mTransactionID=mTransaction->getTransactionID(); }

	SerializedTransaction::pointer getSTransaction() { return mTransaction; }

	const uint256& getID() const					{ return mTransactionID; }
	const RippleAddress& getFromAccount() const		{ return mAccountFrom; }
	STAmount getAmount() const						{ return mTransaction->getFieldU64(sfAmount); }
	STAmount getFee() const							{ return mTransaction->getTransactionFee(); }
	uint32 getFromAccountSeq() const				{ return mTransaction->getSequence(); }
	uint32 getIdent() const							{ return mTransaction->getFieldU32(sfSourceTag); }
	std::vector<unsigned char> getSignature() const	{ return mTransaction->getSignature(); }
	uint32 getLedger() const						{ return mInLedger; }
	TransStatus getStatus() const					{ return mStatus; }

	TER getResult()									{ return mResult; }
	void setResult(TER terResult)					{ mResult = terResult; }

	void setStatus(TransStatus status, uint32 ledgerSeq);
	void setStatus(TransStatus status) { mStatus=status; }
	void setLedger(uint32 ledger) { mInLedger = ledger; }

	// database functions
	static void saveTransaction(const Transaction::pointer&);
	bool save();
	static Transaction::pointer load(const uint256& id);
	static Transaction::pointer findFrom(const RippleAddress& fromID, uint32 seq);

	// conversion function
	static bool convertToTransactions(uint32 ourLedgerSeq, uint32 otherLedgerSeq,
		bool checkFirstTransactions, bool checkSecondTransactions, const SHAMap::SHAMapDiff& inMap,
		std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> >& outMap);

	bool operator<(const Transaction&) const;
	bool operator>(const Transaction&) const;
	bool operator==(const Transaction&) const;
	bool operator!=(const Transaction&) const;
	bool operator<=(const Transaction&) const;
	bool operator>=(const Transaction&) const;

	Json::Value getJson(int options) const;

	static bool isHexTxID(const std::string&);

protected:
	static Transaction::pointer transactionFromSQL(const std::string& statement);
};

#endif
// vim:ts=4
