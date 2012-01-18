#ifndef __LOCALTRANSACTION__
#define __LOCALTRANSACTION__

// A structure to represent a local transaction

#include <string>

#include <boost/shared_ptr.hpp>

#include "uint256.h"
#include "Transaction.h"

class LocalTransaction
{
public:
	typedef boost::shared_ptr<LocalTransaction> pointer;

protected:

	// core specifications
	uint160 mDestAcctID;
	uint64 mAmount;
	uint32 mTag;
	std::string mComment;

	Transaction::pointer mTransaction;

public:

	LocalTransaction(const uint160 &dest, uint64 amount, uint32 tag) :
		mDestAcctID(dest), mAmount(amount), mTag(tag) { ; }
	void setComment(const std::string& comment) { mComment=comment; }

	const uint160& getDestinationAccount() const { return mDestAcctID; }
	uint64 getAmount() const { return mAmount; }
	uint32 getTag() const { return mTag; }
	const std::string& getComment() const { return mComment; }
	Transaction::pointer getTransaction() { return mTransaction; }
	void applyTransaction();

	bool makeTransaction();
};

#endif
