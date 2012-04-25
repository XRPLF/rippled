#ifndef __CONFIRMATION__
#define __CONFIRMATION__

#include "../obj/src/newcoin.pb.h"
#include "uint256.h"
#include <boost/shared_ptr.hpp>

enum ConfirmationStatus
{
	NEW,		// first for this account/seq
	CONFLICTED,	// rejected as of this time
	ACCEPTED,	// in active bundle, has confirmations
	COMMITTED
};
								

class Confirmation
{ // used primarily to report conflicted or rejected transactions
public:
	typedef boost::shared_ptr<Transaction> pointer;

private:
	uint256 mID;
	uint160 mHanko;
	uint64 mTimestamp;
	ConfirmationStatus mStatus;
	bool mConflicts;
	std::vector<unsigned char> mSignature;

public:
	Confirmation();
	Confirmation(const uint256 &id);
	Confirmation(const std::vector<unsigned char> rawConfirmation);

	const uint256& GetID() const { return mID; }
	const uint160& GetHanko() const { return mHanko; }
	uint64 GetTimestamp() const { return mTimestamp; }
	ConfirmationStatus() const { return mStatus; }
	bool HasConflicts() const { return mConflicts; }

	bool save();
};

#endif
