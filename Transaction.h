#ifndef __TRANSACTION__
#define __TRANSACTION__

#include "uint256.h"
#include "newcoin.pb.h"
#include "Hanko.h"
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

	void UpdateHash(void);

public:
	Transaction();
	Transaction(const std::vector<unsigned char> rawTransaction);
	Transaction(const std::string sqlReply);
	Transaction(TransStatus Status, LocalAccount &fromLocal, const Account &from,
		uint32 fromSeq, const uint160 &to, uint64 amount, uint32 ident, uint32 ledger);

	bool Sign(LocalAccount &fromLocalAccount, const Account &fromAccount);
	bool CheckSign(const Account &fromAccount) const;

	bool GetRawUnsigned(std::vector<unsigned char> &raw, const Account &from) const;
	bool GetRawSigned(std::vector<unsigned char> &raw, const Account &from) const;

	const uint256& GetID() const { return mTransactionID; }
	const uint160& GetFromAccount() const { return mAccountFrom; }
	const uint160& GetToAccount() const { return mAccountTo; }
	uint64 GetAmount() const { return mAmount; }
	uint32 GetFromAccountSeq() const { return mFromAccountSeq; }
	uint32 GetSourceLedger() const { return mSourceLedger; }
	uint32 GetIdent() const { return mIdent; }
	const std::vector<unsigned char>& GetSignature() const { return mSignature; }
	uint32 GetLedger() const { return mInLedger; }
	TransStatus GetStatus() const { return mStatus; }

	void SetStatus(TransStatus st);	
	void SetLedger(uint32 Ledger);

	bool operator<(const Transaction &) const;
	bool operator>(const Transaction &) const;
	bool operator==(const Transaction &) const;
	bool operator!=(const Transaction &) const;
	bool operator<=(const Transaction &) const;
	bool operator>=(const Transaction &) const;
};

typedef boost::shared_ptr<Transaction> TransactionPtr;

#endif
