#ifndef INSTANCE_COUNTER__H
#define INSTANCE_COUNTER__H

#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>

#define DEFINE_INSTANCE(x)								\
	extern InstanceType IT_##x;							\
	class Instance_##x : private Instance				\
	{													\
	protected:											\
		Instance_##x() : Instance(IT_##x) { ; }			\
		Instance_##x(const Instance_##x &) :			\
			Instance(IT_##x) { ; }						\
		Instance_##x& operator=(const Instance_##x&)	\
		{ return *this; }								\
	}

#define DECLARE_INSTANCE(x)								\
	InstanceType IT_##x(#x);

#define IS_INSTANCE(x) Instance_##x

class InstanceType
{
protected:
	int						mInstances;
	std::string				mName;
	boost::mutex			mLock;

	InstanceType*			mNextInstance;
	static InstanceType*	sHeadInstance;
	static bool				sMultiThreaded;

public:
	typedef std::pair<std::string, int> InstanceCount;

	InstanceType(const char *n) : mInstances(0), mName(n)
	{
		mNextInstance = sHeadInstance;
		sHeadInstance = this;
	}

	static void multiThread()
	{
		// We can support global objects and multi-threaded code, but not both
		// at the same time. Switch to multi-threaded.
		sMultiThreaded = true;
	}

	void addInstance()
	{
		if (sMultiThreaded)
		{
			mLock.lock();
			++mInstances;
			mLock.unlock();
		}
		else ++mInstances;
	}
	void decInstance()
	{
		if (sMultiThreaded)
		{
			mLock.lock();
			--mInstances;
			mLock.unlock();
		}
		else --mInstances;
	}
	int getCount()
	{
		boost::mutex::scoped_lock(mLock);
		return mInstances;
	}
	const std::string& getName()
	{
		return mName;
	}

	static std::vector<InstanceCount> getInstanceCounts(int min = 1);
};

class Instance
{
protected:
	InstanceType&	mType;

public:
	Instance(InstanceType& t) : mType(t)	{ mType.addInstance(); }
	~Instance()								{ mType.decInstance(); }
};

#endif
