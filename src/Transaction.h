#ifndef __TRANSACTION__
#define __TRANSACTION__

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/cstdint.hpp>

#include "../json/value.h"

#include "key.h"
#include "uint256.h"
#include "../obj/src/newcoin.pb.h"
#include "Hanko.h"
#include "Serializer.h"
#include "SHAMap.h"
#include "SerializedTransaction.h"

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

// This class is for constructing and examining transactions.  Transactions are static so manipulation functions are unnecessary.
class Transaction : public boost::enable_shared_from_this<Transaction>
{
public:
	typedef boost::shared_ptr<Transaction> pointer;

private:
	uint256			mTransactionID;
	NewcoinAddress	mAccountFrom;
	NewcoinAddress	mFromPubKey;	// Sign transaction with this. mSignPubKey
	NewcoinAddress	mSourcePrivate;	// Sign transaction with this.

	uint32			mInLedger;
	TransStatus		mStatus;

	SerializedTransaction::pointer mTransaction;

	Transaction::pointer setClaim(
		const NewcoinAddress& naPrivateKey,
		const std::vector<unsigned char>& vucGenerator,
		const std::vector<unsigned char>& vucPubKey,
		const std::vector<unsigned char>& vucSignature);

	Transaction::pointer setCreate(
		const NewcoinAddress& naPrivateKey,
		const NewcoinAddress& naCreateAccountID,
		uint64 uFund);

	Transaction::pointer setPayment(
		const NewcoinAddress& naPrivateKey,
		const NewcoinAddress& toAccount,
		uint64 uAmount);

public:
	Transaction(const SerializedTransaction::pointer st, bool bValidate);

	static Transaction::pointer sharedTransaction(const std::vector<unsigned char>&vucTransaction, bool bValidate);

	Transaction(
		TransactionType ttKind,
		const NewcoinAddress& naPublicKey,		// To prove transaction is consistent and authorized.
		const NewcoinAddress& naSourceAccount,	// To identify the paying account.
		uint32 uSeq,							// To order transactions.
		uint64 uFee,							// Transaction fee.
		uint32 uSourceTag,						// User call back value.
		uint32 uLedger=0);

	// Claim a wallet.
	static Transaction::pointer sharedClaim(
		const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
		const NewcoinAddress& naSourceAccount,
		uint32 uSourceTag,
		const std::vector<unsigned char>& vucGenerator,
		const std::vector<unsigned char>& vucPubKey,
		const std::vector<unsigned char>& vucSignature);

	// Create an account.
	static Transaction::pointer sharedCreate(
		const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
		const NewcoinAddress& naSourceAccount,
		uint32 uSeq,
		uint64 uFee,
		uint32 uSourceTag,
		uint32 uLedger,
		const NewcoinAddress& naCreateAccountID,	// Account to create.
		uint64 uFund);								// Initial funds in XNC.

	// Make a payment.
	static Transaction::pointer sharedPayment(
		const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
		const NewcoinAddress& naSourceAccount,
		uint32 uSeq,
		uint64 uFee,
		uint32 uSourceTag,
		uint32 uLedger,
		const NewcoinAddress& toAccount,
		uint64 uAmount);

#if 0
	Transaction(const NewcoinAddress& fromID, const NewcoinAddress& toID,
		CKey::pointer pubKey, uint64 uAmount, uint64 fee, uint32 fromSeq, uint32 fromLedger,
		uint32 ident, const std::vector<unsigned char>& signature, uint32 ledgerSeq, TransStatus st);
#endif

	bool sign(const NewcoinAddress& naAccountPrivate);
	bool checkSign() const;
	void updateID() { mTransactionID=mTransaction->getTransactionID(); }

	SerializedTransaction::pointer getSTransaction() { return mTransaction; }

	const uint256& getID() const { return mTransactionID; }
	const NewcoinAddress& getFromAccount() const { return mAccountFrom; }
	uint64 getAmount() const { return mTransaction->getITFieldU64(sfAmount); }
	uint64 getFee() const { return mTransaction->getTransactionFee(); }
	uint32 getFromAccountSeq() const { return mTransaction->getSequence(); }
	uint32 getSourceLedger() const { return mTransaction->getITFieldU32(sfTargetLedger); }
	uint32 getIdent() const { return mTransaction->getITFieldU32(sfSourceTag); }
	std::vector<unsigned char> getSignature() const { return mTransaction->getSignature(); }
	uint32 getLedger() const { return mInLedger; }
	TransStatus getStatus() const { return mStatus; }

	void setStatus(TransStatus status, uint32 ledgerSeq);
	void setStatus(TransStatus status) { mStatus=status; }

	// database functions
	static void saveTransaction(Transaction::pointer);
	bool save() const;
	static Transaction::pointer load(const uint256& id);
	static Transaction::pointer findFrom(const NewcoinAddress& fromID, uint32 seq);

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

	Json::Value getJson(bool decorate, bool paid_local=false, bool credited_local=false) const;

	static bool isHexTxID(const std::string&);

protected:
	static Transaction::pointer transactionFromSQL(const std::string& statement);
#if 0
	Transaction(const uint256& transactionID, const NewcoinAddress& accountFrom, const NewcoinAddress& accountTo,
		 CKey::pointer key, uint64 uAmount, uint64 fee, uint32 fromAccountSeq, uint32 sourceLedger,
		 uint32 ident, const std::vector<unsigned char>& signature, uint32 inLedger, TransStatus status);
#endif
};

#endif
// vim:ts=4
