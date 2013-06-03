#ifndef LOADMANAGER__H
#define LOADMANAGER__H

#include <vector>

#include <boost/thread/mutex.hpp>

enum LoadType
{ // types of load that can be placed on the server

	// Bad things
	LT_InvalidRequest,			// A request that we can immediately tell is invalid
	LT_RequestNoReply,			// A request that we cannot satisfy
	LT_InvalidSignature,		// An object whose signature we had to check and it failed
	LT_UnwantedData,			// Data we have no use for
	LT_BadPoW,					// Proof of work not valid
	LT_BadData,					// Data we have to verify before rejecting

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
private:
    // VFALCO: Make this not a friend
	friend class LoadManager;

public:
	// load source flags
	static const int lsfPrivileged	= 1;
	static const int lsfOutbound	= 2; // outbound connection

public:
	LoadSource(bool admin)
		: mBalance(0)
		, mFlags(admin ? lsfPrivileged : 0)
		, mLastUpdate(UptimeTimer::getInstance().getElapsedSeconds ())
		, mLastWarning(0)
		, mLogged(false)
	{
	}
	
	LoadSource(const std::string& name) 
		: mName(name)
		, mBalance(0)
		, mFlags(0)
		, mLastUpdate(UptimeTimer::getInstance().getElapsedSeconds ())
		, mLastWarning(0)
		, mLogged(false)
	{
	}

	void rename(const std::string& name)	{ mName = name; }
	const std::string& getName()			{ return mName; }

	bool	isPrivileged() const	{ return (mFlags & lsfPrivileged) != 0; }
	void	setPrivileged()			{ mFlags |= lsfPrivileged; }
	int		getBalance() const		{ return mBalance; }

	bool isLogged() const			{ return mLogged; }
	void clearLogged()				{ mLogged = false; }

	void	setOutbound()			{ mFlags |= lsfOutbound; }
	bool	isOutbound() const		{ return (mFlags & lsfOutbound) != 0; }

private:
	std::string	mName;
	int			mBalance;
	int			mFlags;
	int			mLastUpdate;
	int			mLastWarning;
	bool		mLogged;
};


class LoadManager
{ // a collection of load sources
public:

	LoadManager(int creditRate = 100, int creditLimit = 500, int debitWarn = -500, int debitLimit = -1000);
	~LoadManager();
	void init();

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

	void logWarning(const std::string&) const;
	void logDisconnect(const std::string&) const;

	int getCost(LoadType t)		{ return mCosts[static_cast<int>(t)].mCost; }
	void noDeadLock();
	void arm()					{ mArmed = true; }

private:
	int mCreditRate;			// credits gained/lost per second
	int mCreditLimit;			// the most credits a source can have
	int mDebitWarn;				// when a source drops below this, we warn
	int mDebitLimit;			// when a source drops below this, we cut it off (should be negative)

	bool mShutdown;
	bool mArmed;

	/*
	int mSpace1[4];				// We want mUptime to have its own cache line
	int mUptime;
	int mSpace2[4];
	*/

	int mDeadLock;				// Detect server deadlocks

	mutable boost::mutex mLock;

	void canonicalize(LoadSource&, int upTime) const;

	std::vector<LoadCost>	mCosts;

	void addLoadCost(const LoadCost& c) { mCosts[static_cast<int>(c.mType)] = c; }

	void threadEntry();
};

#endif

// vim:ts=4
