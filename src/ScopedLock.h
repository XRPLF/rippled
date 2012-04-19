#ifndef __SCOPEDLOCKHOLDER__
#define __SCOPEDLOCKHOLDER__

#include <boost/thread/recursive_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

// A lock holder that can be returned and copied by value
// When the last reference goes away, the lock is released

class ScopedLock
{
protected:
	mutable boost::shared_ptr<boost::interprocess::scoped_lock<boost::recursive_mutex> > mHolder;

public:
	ScopedLock(boost::recursive_mutex& mutex) :
		mHolder(boost::make_shared<boost::interprocess::scoped_lock<boost::recursive_mutex> >(boost::ref(mutex)))
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
