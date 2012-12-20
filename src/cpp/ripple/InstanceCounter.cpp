#include "InstanceCounter.h"

InstanceType* InstanceType::sHeadInstance = NULL;
bool InstanceType::sMultiThreaded = false;

std::vector<InstanceType::InstanceCount> InstanceType::getInstanceCounts(int min)
{
	std::vector<InstanceCount> ret;
	for (InstanceType* i = sHeadInstance; i != NULL; i = i->mNextInstance)
	{
		int c = i->getCount();
		if (c >= min)
			ret.push_back(InstanceCount(i->getName(), c));
	}
	return ret;
}
