#ifndef __SCOPEDLOCKHOLDER__
#define __SCOPEDLOCKHOLDER__

#include "boost/thread/recursive_mutex.hpp"

// This is a returnable lock holder.
// I don't know why Boost doesn't provide a good way to do this.

class ScopedLock
{
private:
	boost::recursive_mutex *mMutex;	// parent object has greater scope, so guaranteed valid
	mutable bool mValid;

	ScopedLock();		// no implementation

public:
	ScopedLock(boost::recursive_mutex &mutex) : mMutex(&mutex), mValid(true)
	{
		mMutex->lock();
	}

	~ScopedLock()
	{
		if(mValid) mMutex->unlock();
	}

	ScopedLock(const ScopedLock &sl)
	{
		mMutex=sl.mMutex;
		if(sl.mValid)
		{
				mValid=true;
				sl.mValid=false;
		}
		else mValid=false;
	}

	ScopedLock &operator=(const ScopedLock &sl)
	{ // we inherit any lock the other class member had
		if(mMutex!=sl.mMutex)
		{
			if(mValid) mMutex->unlock();
			mMutex=sl.mMutex;
			mMutex->lock();
			mValid=true;
		}
		return *this;
	}

	void unlock(void)
	{
		if(mValid)
		{
			mMutex->unlock();
			mValid=false;
		}
	}

	void lock(void)
	{
		if(mValid)
		{
			mMutex->lock();
			mValid=true;
		}
	}
};

#endif
