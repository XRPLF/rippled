#ifndef FEATURETABLE__H
#define FEATURETABLE__H

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/thread/mutex.hpp>

#include "../json/value.h"

#include "uint256.h"

class FeatureSet
{ // the status of all features requested in a given window
public:
	uint32	mCloseTime;
	int		mTrustedValidations;				// number of trusted validations
	boost::unordered_map<uint256, int> mVotes;	// yes votes by feature

	FeatureSet(uint32 ct, int tv) : mCloseTime(ct), mTrustedValidations(tv) { ; }
	void addVote(const uint256& feature)	{ ++mVotes[feature]; }
};

class FeatureTable
{
protected:

	class FeatureState
	{
	public:
		bool	mVetoed;			// We don't want this feature enabled
		bool	mEnabled;
		bool	mSupported;

		uint32	mFirstMajority;		// First time we saw a majority (close time)
		uint32	mLastMajority;		// Most recent time we saw a majority (close time)

		FeatureState() : mVetoed(false), mEnabled(false), mSupported(false), mFirstMajority(0), mLastMajority(0) { ; }
	};

	typedef boost::unordered_map<uint256, FeatureState> featureMap_t;
	typedef std::pair<const uint256, FeatureState> featureIt_t;
	typedef boost::unordered_set<uint256> featureList_t;

	boost::mutex	mMutex;
	featureMap_t	mFeatureMap;
	int				mMajorityTime;		// Seconds a feature must hold a majority
	int				mMajorityFraction;	// 256 = 100%
	uint32			mFirstReport;		// close time of first majority report
	uint32			mLastReport;		// close time of most recent majority report

	FeatureState*	getCreateFeature(const uint256& feature, bool create);
	bool shouldEnable (uint32 closeTime, const FeatureState& fs);

public:

	FeatureTable(uint32 majorityTime, int majorityFraction)
		: mMajorityTime(majorityTime), mMajorityFraction(majorityFraction), mFirstReport(0)
	{ ; }

	bool vetoFeature(const uint256& feature);
	bool unVetoFeature(const uint256& feature);

	bool enableFeature(const uint256& feature);
	bool disableFeature(const uint256& feature);

	bool isFeatureEnabled(const uint256& feature);

	void setEnabledFeatures(const std::vector<uint256>& features);
	void setSupportedFeatures(const std::vector<uint256>& features);

	featureList_t getVetoedFeatures();
	featureList_t getEnabledFeatures();
	featureList_t getFeaturesToEnable(uint32 closeTime);	// gets features we would vote to enable

	void reportValidations(const FeatureSet&);

	Json::Value getJson(int);
};

#endif
