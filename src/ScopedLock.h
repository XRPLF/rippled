#ifndef __SCOPEDLOCKHOLDER__
#define __SCOPEDLOCKHOLDER__

#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

// A lock holder that can be returned and copied by value
// When the last reference goes away, the lock is released

class ScopedLock
{
protected:
	mutable boost::shared_ptr<boost::recursive_mutex::scoped_lock> mHolder;

public:
	ScopedLock(boost::recursive_mutex& mutex) :
		mHolder(boost::make_shared<boost::recursive_mutex::scoped_lock>(boost::ref(mutex)))
	{ ;	}
	void lock() const
	{
		mHolder->lock();
	}
	void unlock() const
	{
		mHolder->unlock();
	}
};

#endif
