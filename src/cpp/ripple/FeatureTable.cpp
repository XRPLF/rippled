#include "FeatureTable.h"

FeatureTable::FeatureState* FeatureTable::getCreateFeature(const uint256& feature, bool create)
{ // call with the mutex held
	featureMap_t::iterator it = mFeatureMap.find(feature);
	if (it == mFeatureMap.end())
	{
		if (!create)
			return NULL;
		return &(mFeatureMap[feature]);
	}
	return &(it->second);
}

bool FeatureTable::vetoFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, true);
	if (s->mVetoed)
		return false;
	s->mVetoed = true;
	return true;
}

bool FeatureTable::unVetoFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, false);
	if (!s || !s->mVetoed)
		return false;
	s->mVetoed = false;
	return true;
}

bool FeatureTable::enableFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, true);
	if (s->mEnabled)
		return false;
	s->mEnabled = true;
	return true;
}

bool FeatureTable::disableFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, false);
	if (!s || !s->mEnabled)
		return false;
	s->mEnabled = false;
	return true;
}

bool FeatureTable::isFeatureEnabled(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, false);
	return s && s->mEnabled;
}

