#ifndef __TRANSACTION__
#define __TRANSACTION__

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/cstdint.hpp>

#include "json/value.h"

#include "key.h"
#include "uint256.h"
#include "newcoin.pb.h"
#include "Hanko.h"
#include "Serializer.h"
#include "Wallet.h"
#include "SHAMap.h"

/*
We could have made something that inherited from the protobuf transaction but this seemed simpler
*/

enum TransStatus
{
	NEW			=0,	// just received / generated
	INVALID		=1,	// no valid signature, insufficient funds
	INCLUDED	=2,	// added to the current ledger
	CONFLICTED	=3,	// losing to a conflicting transaction
	COMMITTED	=4,	// known to be in a ledger
	HELD		=5,	// not valid now, maybe later
	REMOVED		=6,	// taken out of a ledger
	OBSOLETE	=7, // a compatible transaction has taken precedence
	INCOMPLETE	=8  // needs more signatures
};

class Transaction : public boost::enable_shared_from_this<Transaction>
{
public:

	typedef boost::shared_ptr<Transaction> pointer;

	static const uint32 TransSignMagic=0x54584E00; // "TXN"

private:
	uint256			mTransactionID;
	uint160			mAccountFrom, mAccountTo;
	uint64			mAmount, mFee;
	uint32			mFromAccountSeq, mSourceLedger, mIdent;
	CKey::pointer	mFromPubKey;
	std::vector<unsigned char> mSignature;

	uint32		mInLedger;
	TransStatus	mStatus;

public:
	Transaction();
	Transaction(const std::vector<unsigned char>& rawTransaction, bool validate);
	Transaction(LocalAccount::pointer fromLocal, const uint160& to, uint64 amount, uint32 ident, uint32 ledger);

	bool sign(LocalAccount::pointer fromLocalAccount);
	bool checkSign() const;
	void updateID();
	void updateFee();

	Serializer::pointer getRaw(bool prefix) const;
	Serializer::pointer getSigned() const;

	const uint256& getID() const { return mTransactionID; }
	const uint160& getFromAccount() const { return mAccountFrom; }
	const uint160& getToAccount() const { return mAccountTo; }
	uint64 getAmount() const { return mAmount; }
	uint64 getFee() const { return mFee; }
	uint32 getFromAccountSeq() const { return mFromAccountSeq; }
	uint32 getSourceLedger() const { return mSourceLedger; }
	uint32 getIdent() const { return mIdent; }
	const std::vector<unsigned char>& getSignature() const { return mSignature; }
	uint32 getLedger() const { return mInLedger; }
	TransStatus getStatus() const { return mStatus; }

	void setStatus(TransStatus status, uint32 ledgerSeq);
	void setStatus(TransStatus status) { mStatus=status; }

	// database functions
	bool save() const;
	static Transaction::pointer load(const uint256& id);
	static Transaction::pointer findFrom(const uint160& fromID, uint32 seq);

	// conversion function
	static bool convertToTransactions(uint32 ourLedgerSeq, uint32 otherLedgerSeq,
		bool checkFirstTransactions, bool checkSecondTransactions,
		const std::map<uint256, std::pair<SHAMapItem::pointer,SHAMapItem::pointer> >& inMap,
		std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> >& outMap);

	bool operator<(const Transaction&) const;
	bool operator>(const Transaction&) const;
	bool operator==(const Transaction&) const;
	bool operator!=(const Transaction&) const;
	bool operator<=(const Transaction&) const;
	bool operator>=(const Transaction&) const;

	Json::Value getJson(bool decorate) const;

protected:
	static Transaction::pointer transactionFromSQL(const std::string& statement);
	Transaction(const uint256& transactionID, const uint160& accountFrom, const uint160& accountTo,
		 CKey::pointer key, uint64 amount, uint64 fee, uint32 fromAccountSeq, uint32 sourceLedger,
		 uint32 ident, const std::vector<unsigned char>& signature, uint32 inLedger, TransStatus status) :
		 	mTransactionID(transactionID), mAccountFrom(accountFrom), mAccountTo(accountTo),
		 	mAmount(amount), mFee(fee), mFromAccountSeq(fromAccountSeq), mSourceLedger(sourceLedger),
			mIdent(ident), mFromPubKey(key), mSignature(signature), mInLedger(inLedger), mStatus(status)
	{ ; }

};

#endif
