#include "FeatureTable.h"

#include <boost/foreach.hpp>

#include "Log.h"

SETUP_LOG();

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

FeatureTable::featureList_t FeatureTable::getVetoedFeatures()
{
	featureList_t ret;
	boost::mutex::scoped_lock sl(mMutex);
	BOOST_FOREACH(const featureIt_t& it, mFeatureMap)
	{
		if (it.second.mVetoed)
			ret.insert(it.first);
	}
	return ret;
}

FeatureTable::featureList_t FeatureTable::getEnabledFeatures()
{
	featureList_t ret;
	boost::mutex::scoped_lock sl(mMutex);
	BOOST_FOREACH(const featureIt_t& it, mFeatureMap)
	{
		if (it.second.mEnabled)
			ret.insert(it.first);
	}
	return ret;
}

bool FeatureTable::shouldEnable(uint32 closeTime, const FeatureState& fs)
{
	if (fs.mVetoed || fs.mEnabled || !fs.mSupported || (fs.mLastMajority != mLastReport))
		return false;

	if (fs.mFirstMajority == mFirstReport)
	{ // had a majority when we first started the server, relaxed check
		// WRITEME
	}
	else
	{ // didn't have a majority when we first started the server, normal check
		// WRITEME
	}

	return true;

}

FeatureTable::featureList_t FeatureTable::getFeaturesToEnable(uint32 closeTime)
{
	featureList_t ret;
	boost::mutex::scoped_lock sl(mMutex);
	if (mLastReport != 0)
	{
		BOOST_FOREACH(const featureIt_t& it, mFeatureMap)
		{
			if (shouldEnable(closeTime, it.second))
				ret.insert(it.first);
		}
	}
	return ret;
}

void FeatureTable::reportValidations(const FeatureSet& set)
{
	if (set.mTrustedValidations == 0)
		return;
	int threshold = (set.mTrustedValidations * mMajorityFraction) / 256;

	typedef std::map<uint256, int>::value_type u256_int_pair;

	boost::mutex::scoped_lock sl(mMutex);

	if (mFirstReport == 0)
		mFirstReport = set.mCloseTime;
	BOOST_FOREACH(const u256_int_pair& it, set.mVotes)
	{
		FeatureState& state = mFeatureMap[it.first];
		cLog(lsDEBUG) << "Feature " << it.first.GetHex() << " has " << it.second << " votes, needs " << threshold;
		if (it.second >= threshold)
		{ // we have a majority
			state.mLastMajority = set.mCloseTime;
			if (state.mFirstMajority == 0)
			{
				cLog(lsWARNING) << "Feature " << it.first << " attains a majority vote";
				state.mFirstMajority = set.mCloseTime;
			}
		}
		else // we have no majority
		{
			if (state.mFirstMajority != 0)
			{
				cLog(lsWARNING) << "Feature " << it.first << " loses majority vote";
				state.mFirstMajority = 0;
				state.mLastMajority = 0;
			}
		}
	}
	mLastReport = set.mCloseTime;
}

void FeatureTable::setEnabledFeatures(const std::vector<uint256>& features)
{
	boost::mutex::scoped_lock sl(mMutex);
	BOOST_FOREACH(featureIt_t& it, mFeatureMap)
	{
		it.second.mEnabled = false;
	}
	BOOST_FOREACH(const uint256& it, features)
	{
		mFeatureMap[it].mEnabled = true;
	}
}

void FeatureTable::setSupportedFeatures(const std::vector<uint256>& features)
{
	boost::mutex::scoped_lock sl(mMutex);
	BOOST_FOREACH(featureIt_t& it, mFeatureMap)
	{
		it.second.mSupported = false;
	}
	BOOST_FOREACH(const uint256& it, features)
	{
		mFeatureMap[it].mSupported = true;
	}
}

Json::Value FeatureTable::getJson(int)
{
	Json::Value ret(Json::objectValue);
	{
		boost::mutex::scoped_lock sl(mMutex);
		BOOST_FOREACH(const featureIt_t& it, mFeatureMap)
		{
			Json::Value v(Json::objectValue);

			v["supported"] = it.second.mSupported ? "true" : "false";

			if (it.second.mEnabled)
				v["enabled"] = "true";
			else
			{
				v["enabled"] = "false";
				if (mLastReport != 0)
				{
					if (it.second.mLastMajority == 0)
						v["majority"] = "no";
					else
					{
						if (it.second.mFirstMajority != 0)
						{
							if (it.second.mFirstMajority == mFirstReport)
								v["majority_start"] = "start";
							else
								v["majority_start"] = it.second.mFirstMajority;
						}
						if (it.second.mLastMajority != 0)
						{
							if (it.second.mLastMajority == mLastReport)
								v["majority_until"] = "now";
							else
								v["majority_until"] = it.second.mLastMajority;
						}
					}
				}
			}

			if (it.second.mVetoed)
				v["veto"] = "true";

			ret[it.first.GetHex()] = v;
		}
	}

	return ret;
}

// vim:ts=4
