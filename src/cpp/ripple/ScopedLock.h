#ifndef __SCOPEDLOCKHOLDER__
#define __SCOPEDLOCKHOLDER__

#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

typedef boost::recursive_mutex::scoped_lock ScopedLock;

// A lock holder that can be returned and copied by value
// When the last reference goes away, the lock is released

class SharedScopedLock
{
protected:
	mutable boost::shared_ptr<boost::recursive_mutex::scoped_lock> mHolder;

public:
	SharedScopedLock(boost::recursive_mutex& mutex) :
		mHolder(boost::make_shared<boost::recursive_mutex::scoped_lock>(boost::ref(mutex)))	{ ;	}

	void lock() const	{ mHolder->lock(); }
	void unlock() const	{ mHolder->unlock(); }
};

// A class that unlocks on construction and locks on destruction

class ScopedUnlock
{
protected:
	bool mUnlocked;
	boost::recursive_mutex& mMutex;

public:
	ScopedUnlock(boost::recursive_mutex& mutex, bool unlock = true) : mUnlocked(unlock), mMutex(mutex)
	{
		if (unlock)
			mMutex.unlock();
	}

	~ScopedUnlock()
	{
		if (mUnlocked)
			mMutex.lock();
	}

	void lock()
	{
		if (mUnlocked)
		{
			mMutex.lock();
			mUnlocked = false;
		}
	}

	void unlock()
	{
		if (!mUnlocked)
		{
			mUnlocked = true;
			mMutex.unlock();
		}
	}

private:
	ScopedUnlock(const ScopedUnlock&); // no implementation
	ScopedUnlock& operator=(const ScopedUnlock&); // no implementation
};

#endif
