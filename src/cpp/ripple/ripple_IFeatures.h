#ifndef RIPPLE_IFEATURES_H
#define RIPPLE_IFEATURES_H

class FeatureSet
{ // the status of all features requested in a given window
public:
	uint32	mCloseTime;
	int		mTrustedValidations;				// number of trusted validations
	boost::unordered_map<uint256, int> mVotes;	// yes votes by feature

	FeatureSet(uint32 ct, int tv) : mCloseTime(ct), mTrustedValidations(tv) { ; }
	void addVote(const uint256& feature)	{ ++mVotes[feature]; }
};

class FeatureState
{
public:
	bool		mVetoed;			// We don't want this feature enabled
	bool		mEnabled;
	bool		mSupported;
	bool		mDefault;			// Include in genesis ledger

	uint32		mFirstMajority;		// First time we saw a majority (close time)
	uint32		mLastMajority;		// Most recent time we saw a majority (close time)

	std::string	mFriendlyName;

	FeatureState()
		: mVetoed(false), mEnabled(false), mSupported(false), mDefault(false),
		mFirstMajority(0), mLastMajority(0)	{ ; }

	void setVeto()								{ mVetoed = true; }
	void setDefault()							{ mDefault = true; }
	bool isDefault()							{ return mDefault; }
	bool isSupported()							{ return mSupported; }
	bool isVetoed()								{ return mVetoed; }
	bool isEnabled()							{ return mEnabled; }
	const std::string& getFiendlyName()			{ return mFriendlyName; }
	void setFriendlyName(const std::string& n)	{ mFriendlyName = n; }
};

/** Feature table interface.

	The feature table stores the list of enabled and potential features.
	Individuals features are voted on by validators during the consensus
	process.
*/
class IFeatures
{
public:
	static IFeatures* New (uint32 majorityTime, int majorityFraction);

	virtual ~IFeatures () { }

	virtual void addInitialFeatures() = 0;

	virtual FeatureState* addKnownFeature(const char *featureID, const char *friendlyName, bool veto) = 0;
	virtual uint256 getFeature(const std::string& name) = 0;

	virtual bool vetoFeature(const uint256& feature) = 0;
	virtual bool unVetoFeature(const uint256& feature) = 0;

	virtual bool enableFeature(const uint256& feature) = 0;
	virtual bool disableFeature(const uint256& feature) = 0;

	virtual bool isFeatureEnabled(const uint256& feature) = 0;
	virtual bool isFeatureSupported(const uint256& feature) = 0;

	virtual void setEnabledFeatures(const std::vector<uint256>& features) = 0;
	virtual void setSupportedFeatures(const std::vector<uint256>& features) = 0;

    // VFALCO: NOTE these can't possibly be used since featureList_t was/is private.
    /*
	featureList_t getVetoedFeatures() = 0;
	featureList_t getEnabledFeatures() = 0;
	featureList_t getFeaturesToEnable(uint32 closeTime) = 0;	// gets features we would vote to enable
	featureList_t getDesiredFeatures() = 0;						// features we support, do not veto, are not enabled
    */

	virtual void reportValidations(const FeatureSet&) = 0;

	virtual Json::Value getJson(int) = 0;
	virtual Json::Value getJson(const uint256&) = 0;

	virtual void doValidation(Ledger::ref lastClosedLedger, STObject& baseValidation) = 0;
	virtual void doVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) = 0;

};

#endif
