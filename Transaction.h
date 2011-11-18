#ifndef __TRANSACTION__
#define __TRANSACTION__

#include "uint256.h"
#include "newcoin.pb.h"
#include "Hanko.h"
#include "Serializer.h"
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/cstdint.hpp>

/*
We could have made something that inherited from the protobuf transaction but this seemed simpler
*/

enum TransStatus
{
	NEW,		// just received / generated
	INVALID,	// no valid signature, insufficient funds
	INCLUDED,	// added to the current ledger
	CONFLICTED,	// losing to a conflicting transaction
	COMMITTED,	// known to be in a ledger
	HELD,		// not valid now, maybe later
};

class Account;
class LocalAccount;

class Transaction : public boost::enable_shared_from_this<Transaction>
{
public:
	static const uint32 TransSignMagic=0x54584E00; // "TXN"

private:
	uint256		mTransactionID;
	uint160		mAccountFrom, mAccountTo;
	uint64		mAmount;
	uint32		mFromAccountSeq, mSourceLedger, mIdent;
	std::vector<unsigned char> mSignature;

	uint32		mInLedger;
	TransStatus	mStatus;

public:
	Transaction();
	Transaction(const std::vector<unsigned char> rawTransaction);
	Transaction(const std::string sqlReply);
	Transaction(TransStatus Status, LocalAccount& fromLocal, Account& from,
		uint32 fromSeq, const uint160& to, uint64 amount, uint32 ident, uint32 ledger);

	bool sign(LocalAccount& fromLocalAccount, Account& fromAccount);
	bool checkSign(Account& fromAccount) const;
	void updateID(Account& fromAccount);

	Serializer::pointer getRawUnsigned(Account& from) const;
	Serializer::pointer getRawSigned(Account& from) const;

	const uint256& getID() const { return mTransactionID; }
	const uint160& getFromAccount() const { return mAccountFrom; }
	const uint160& getToAccount() const { return mAccountTo; }
	uint64 getAmount() const { return mAmount; }
	uint32 getFromAccountSeq() const { return mFromAccountSeq; }
	uint32 getSourceLedger() const { return mSourceLedger; }
	uint32 getIdent() const { return mIdent; }
	const std::vector<unsigned char>& getSignature() const { return mSignature; }
	uint32 getLedger() const { return mInLedger; }
	TransStatus getStatus() const { return mStatus; }

	void setStatus(TransStatus st);	
	void setLedger(uint32 Ledger);

	bool operator<(const Transaction &) const;
	bool operator>(const Transaction &) const;
	bool operator==(const Transaction &) const;
	bool operator!=(const Transaction &) const;
	bool operator<=(const Transaction &) const;
	bool operator>=(const Transaction &) const;
};

typedef boost::shared_ptr<Transaction> TransactionPtr;

#endif
