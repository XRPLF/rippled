#ifndef INSTANCE_COUNTER__H
#define INSTANCE_COUNTER__H

#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>

#define DEFINE_INSTANCE(x)							\
	extern InstanceType IT_##x;						\
	class Instance_##x : private Instance			\
	{												\
	protected:										\
		Instance_##x() : Instance(IT_##x) { ; }		\
	}

#define DECLARE_INSTANCE(x)							\
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

public:
	typedef std::pair<std::string, int> InstanceCount;

	InstanceType(const char *n) : mInstances(0), mName(n)
	{
		mNextInstance = sHeadInstance;
		sHeadInstance = this;
	}

	void addInstance()
	{
		mLock.lock();
		++mInstances;
		mLock.unlock();
	}
	void decInstance()
	{
		mLock.lock();
		--mInstances;
		mLock.unlock();
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
