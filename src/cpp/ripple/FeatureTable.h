#ifndef FEATURETABLE__H
#define FEATURETABLE__H

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/thread/mutex.hpp>

#include "uint256.h"

struct FeatureSet
{ // the status of all features requested in a given window
	uint32	mLedgerSequence;
	uint32	mCloseTime;
	int		mTrustedValidations;				// number of trusted validations

	boost::unordered_map<uint256, int> mVotes;	// yes votes by feature
};

class FeatureTable
{
protected:

	class FeatureState
	{
	public:
		bool	mVetoed;			// We don't want this feature enabled
		bool	mEnabled;

		int		mEnableVotes;		// Trusted votes to enable this feature
		int		mDisableVotes;		// Trusted votes to disable this feature
		uint32	mFirstMajority;		// First time we saw a majority (ledger sequence)
		uint32	mLastMajority;		// Most recent time we saw a majority

		FeatureState() : mVetoed(false), mEnabled(false), mEnableVotes(0), mDisableVotes(0),
			mFirstMajority(0), mLastMajority(0) { ; }
	};

	typedef boost::unordered_map<uint256, FeatureState> featureMap_t;
	typedef boost::unordered_set<uint256> featureList_t;

	boost::mutex	mMutex;
	featureMap_t	mFeatureMap;
	int				mMajorityTime;		// Seconds a feature must hold a majority
	int				mMajorityFraction;	// 256 = 100%

	FeatureState*	getCreateFeature(const uint256& feature, bool create);

public:

	FeatureTable(uint32 majorityTime, int mMajorityFraction) : mMajorityTime(majorityTime) { ; }

	bool vetoFeature(const uint256& feature);
	bool unVetoFeature(const uint256& feature);

	bool enableFeature(const uint256& feature);
	bool disableFeature(const uint256& feature);

	bool isFeatureEnabled(const uint256& feature);

	featureList_t getVetoedFeatures();
	featureList_t getEnabledFeatures();
	featureList_t getFeaturesToEnable(uint32 sequence);	// gets features we would vote to enable

	void reportValidations(const FeatureSet&);
};

#endif
