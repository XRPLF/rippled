#ifndef __SCOPEDLOCKHOLDER__
#define __SCOPEDLOCKHOLDER__

#include "boost/thread/mutex.hpp"

// This is a returnable lock holder.
// I don't know why Boost doesn't provide a good way to do this.

class ScopedLock
{
private:
    boost::mutex *mMutex;	// parent object has greater scope, so guaranteed valid
    mutable bool mValid;

    ScopedLock();		// no implementation
    
public:
    ScopedLock(boost::mutex &mutex) : mMutex(&mutex), mValid(true)
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
        if(mValid) mMutex->unlock();
        mMutex=sl.mMutex;
        if(sl.mValid)
        {
            if(mValid) mMutex->unlock();
            mValid=true;
            sl.mValid=false;
        }
    }
};

#endif
