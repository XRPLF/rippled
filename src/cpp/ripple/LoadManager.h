#ifndef LOADSOURCE__H
#define LOADSOURCE__H

#include <boost/thread/mutex.hpp>

class LoadSource
{ // a single endpoint that can impose load
	friend class LoadManager;

public:

	// load source flags
	static const int lsfPrivileged	= 1;

protected:
	int		mBalance;
	int		mFlags;
	time_t	mLastUpdate;
	time_t	mLastWarning;

public:
	LoadSource() : mBalance(0), mFlags(0), mLastUpdate(0), mLastWarning(0)
									{ ; }

	bool	isPrivileged() const	{ return (mFlags & lsfPrivileged) != 0; }
	void	setPrivileged()			{ mFlags |= lsfPrivileged; }
	int		getBalance() const		{ return mBalance; }
};


class LoadManager
{ // a collection of load sources
protected:

	int mCreditRate;			// credits gained/lost per second
	int mCreditLimit;			// the most credits a source can have
	int	mDebitWarn;				// when a source drops below this, we warn
	int mDebitLimit;			// when a source drops below this, we cut it off

	boost::mutex	mLock;

public:

	LoadManager(int creditRate, int creditLimit, int debitWarn, int debitLimit) :
		mCreditRate(creditRate), mCreditLimit(creditLimit), mDebitWarn(debitWarn), mDebitLimit(debitLimit) { ; }

	int getCreditRate();
	int getCreditLimit();
	int getDebitWarn();
	int getDebitLimit();
	void setCreditRate(int);
	void setCreditLimit(int);
	void setDebitWarn(int);
	void setDebitLimit(int);

	bool shouldWarn(const LoadSource&);
	bool shouldCutoff(const LoadSource&);
	void credit(LoadSource&, int credits);
	bool debit(LoadSource&, int credits);	// return value: false = balance okay, true = warn/cutoff
};

#endif
