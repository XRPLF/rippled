#ifndef LOADMANAGER__H
#define LOADMANAGER__H

#include <vector>

#include <boost/thread/mutex.hpp>

#include "../json/value.h"

#include "types.h"

enum LoadType
{ // types of load that can be placed on the server

	// Bad things
	LT_InvalidRequest,			// A request that we can immediately tell is invalid
	LT_RequestNoReply,			// A request that we cannot satisfy
	LT_InvalidSignature,		// An object whose signature we had to check and it failed
	LT_UnwantedData,			// Data we have no use for
	LT_BadPoW,					// Proof of work not valid

	// Good things
	LT_NewTrusted,				// A new transaction/validation/proposal we trust
	LT_NewTransaction,			// A new, valid transaction
	LT_NeededData,				// Data we requested

	// Requests
	LT_RequestData,				// A request that is hard to satisfy, disk access
	LT_CheapQuery,				// A query that is trivial, cached data

	LT_MAX						// MUST BE LAST
};

// load categories
static const int LC_Disk	= 1;
static const int LC_CPU		= 2;
static const int LC_Network	= 4;

class LoadCost
{
public:
	LoadType	mType;
	int			mCost;
	int			mCategories;

	LoadCost() : mType(), mCost(0), mCategories(0) { ; }
	LoadCost(LoadType t, int cost, int cat) : mType(t), mCost(cost), mCategories(cat) { ; }
};

class LoadSource
{ // a single endpoint that can impose load
	friend class LoadManager;

public:

	// load source flags
	static const int lsfPrivileged	= 1;
	static const int lsfOutbound	= 2; // outbound connection

protected:
	int		mBalance;
	int		mFlags;
	time_t	mLastUpdate;
	time_t	mLastWarning;

public:
	LoadSource() : mBalance(0), mFlags(0), mLastWarning(0)
									{ mLastUpdate = time(NULL); }

	bool	isPrivileged() const	{ return (mFlags & lsfPrivileged) != 0; }
	void	setPrivileged()			{ mFlags |= lsfPrivileged; }
	int		getBalance() const		{ return mBalance; }

	void	setOutbound()			{ mFlags |= lsfOutbound; }
	bool	isOutbound() const		{ return (mFlags & lsfOutbound) != 0; }
};


class LoadManager
{ // a collection of load sources
protected:

	int mCreditRate;			// credits gained/lost per second
	int mCreditLimit;			// the most credits a source can have
	int mDebitWarn;				// when a source drops below this, we warn
	int mDebitLimit;			// when a source drops below this, we cut it off (should be negative)

	mutable boost::mutex mLock;

	void canonicalize(LoadSource&, const time_t now) const;

	std::vector<LoadCost>	mCosts;

	void addLoadCost(const LoadCost& c) { mCosts[static_cast<int>(c.mType)] = c; }

public:

	LoadManager(int creditRate = 10, int creditLimit = 50, int debitWarn = -50, int debitLimit = -100);

	int getCreditRate() const;
	int getCreditLimit() const;
	int getDebitWarn() const;
	int getDebitLimit() const;
	void setCreditRate(int);
	void setCreditLimit(int);
	void setDebitWarn(int);
	void setDebitLimit(int);

	bool shouldWarn(LoadSource&) const;
	bool shouldCutoff(LoadSource&) const;
	bool adjust(LoadSource&, int credits) const; // return value: false=balance okay, true=warn/cutoff
	bool adjust(LoadSource&, LoadType l) const;

	int getCost(LoadType t)		{ return mCosts[static_cast<int>(t)].mCost; }
};

class LoadFeeTrack
{ // structure that tracks our current fee/load schedule
protected:

	static const int lftNormalFee = 256;		// 256 is the minimum/normal load factor
	static const int lftFeeIncFraction = 16;	// increase fee by 1/16
	static const int lftFeeDecFraction = 16;	// decrease fee by 1/16	
	static const int lftFeeMax = lftNormalFee * 1000000;

	uint32 mBaseRef;				// The number of fee units a reference transaction costs
	uint32 mBaseFee;				// The cost in millionths of a ripple of a reference transaction
	uint32 mLocalTxnLoadFee;		// Scale factor, lftNormalFee = normal fee
	uint32 mRemoteTxnLoadFee;		// Scale factor, lftNormalFee = normal fee

	boost::mutex mLock;

	static uint64 mulDiv(uint64 value, uint32 mul, uint32 div);

public:

	LoadFeeTrack()	: mLocalTxnLoadFee(lftNormalFee), mRemoteTxnLoadFee(lftNormalFee) { ; }

	uint64 scaleFeeBase(uint64 fee);	// Scale from fee units to millionths of a ripple
	uint64 scaleFeeLoad(uint64 fee);	// Scale using load as well as base rate

	uint32 getRemoteFee();
	uint32 getLocalFee();
	uint32 getBaseRef();
	uint32 getBaseFee();

	Json::Value getJson(int);

	void setBaseFee(uint32);
	void setRemoteFee(uint32);
	void raiseLocalFee();
	void lowerLocalFee();
};


#endif

// vim:ts=4
