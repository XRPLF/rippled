#ifndef __SCOPEDLOCKHOLDER__
#define __SCOPEDLOCKHOLDER__

#include <boost/thread/recursive_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/shared_ptr.hpp>

class ScopedLock
{
protected:
	mutable boost::shared_ptr<boost::interprocess::scoped_lock<boost::recursive_mutex> > mHolder;

public:
	ScopedLock(boost::recursive_mutex &mutex) :
		mHolder(new boost::interprocess::scoped_lock<boost::recursive_mutex>(mutex))
	{ ; }
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
