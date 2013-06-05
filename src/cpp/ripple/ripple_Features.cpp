
class Features;

SETUP_LOG (Features)

class FeatureSet
{ // the status of all features requested in a given window
public:
	uint32	mCloseTime;
	int		mTrustedValidations;				// number of trusted validations
	boost::unordered_map<uint256, int> mVotes;	// yes votes by feature

	FeatureSet(uint32 ct, int tv) : mCloseTime(ct), mTrustedValidations(tv) { ; }
	void addVote(const uint256& feature)	{ ++mVotes[feature]; }
};

// VFALCO: TODO, inline all function definitions
class Features : public IFeatures
{
private:
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

	Features(uint32 majorityTime, int majorityFraction)
		: mMajorityTime(majorityTime), mMajorityFraction(majorityFraction), mFirstReport(0), mLastReport(0)
	{ addInitialFeatures(); }

	void addInitialFeatures();

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
	featureList_t getDesiredFeatures();						// features we support, do not veto, are not enabled

	void reportValidations(const FeatureSet&);

	Json::Value getJson(int);

	void doValidation(Ledger::ref lastClosedLedger, STObject& baseValidation);
	void doVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition);
};

void Features::addInitialFeatures()
{
	// For each feature this version supports, call enableFeature.
	// Permanent vetos can also be added here.
}

Features::FeatureState* Features::getCreateFeature(const uint256& featureHash, bool create)
{ // call with the mutex held
	featureMap_t::iterator it = mFeatureMap.find(featureHash);
	if (it == mFeatureMap.end())
	{
		if (!create)
			return NULL;
		FeatureState *feature = &(mFeatureMap[featureHash]);

		{
			std::string query = "SELECT FirstMajority,LastMajority FROM Features WHERE hash='";
			query.append(featureHash.GetHex());
			query.append("';");

			ScopedLock sl(theApp->getWalletDB()->getDBLock());
			Database* db = theApp->getWalletDB()->getDB();
			if (db->executeSQL(query) && db->startIterRows())
			{
				feature->mFirstMajority = db->getBigInt("FirstMajority");
				feature->mLastMajority = db->getBigInt("LastMajority");
				db->endIterRows();
			}
		}

		return feature;
	}
	return &(it->second);
}

bool Features::vetoFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, true);
	if (s->mVetoed)
		return false;
	s->mVetoed = true;
	return true;
}

bool Features::unVetoFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, false);
	if (!s || !s->mVetoed)
		return false;
	s->mVetoed = false;
	return true;
}

bool Features::enableFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, true);
	if (s->mEnabled)
		return false;
	s->mEnabled = true;
	return true;
}

bool Features::disableFeature(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, false);
	if (!s || !s->mEnabled)
		return false;
	s->mEnabled = false;
	return true;
}

bool Features::isFeatureEnabled(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, false);
	return s && s->mEnabled;
}

Features::featureList_t Features::getVetoedFeatures()
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

Features::featureList_t Features::getEnabledFeatures()
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

bool Features::shouldEnable(uint32 closeTime, const FeatureState& fs)
{
	if (fs.mVetoed || fs.mEnabled || !fs.mSupported || (fs.mLastMajority != mLastReport))
		return false;

	if (fs.mFirstMajority == mFirstReport)
	{ // had a majority when we first started the server, relaxed check
		// WRITEME
	}
	// didn't have a majority when we first started the server, normal check
	return (fs.mLastMajority - fs.mFirstMajority) > mMajorityTime;

}

Features::featureList_t Features::getFeaturesToEnable(uint32 closeTime)
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

Features::featureList_t Features::getDesiredFeatures()
{
	featureList_t ret;
	boost::mutex::scoped_lock sl(mMutex);
	BOOST_FOREACH(const featureIt_t& it, mFeatureMap)
	{
		if (it.second.mSupported && !it.second.mEnabled && !it.second.mVetoed)
			ret.insert(it.first);
	}
	return ret;
}

void Features::reportValidations(const FeatureSet& set)
{
	if (set.mTrustedValidations == 0)
		return;
	int threshold = (set.mTrustedValidations * mMajorityFraction) / 256;

	typedef std::map<uint256, int>::value_type u256_int_pair;

	boost::mutex::scoped_lock sl(mMutex);

	if (mFirstReport == 0)
		mFirstReport = set.mCloseTime;

	std::vector<uint256> changedFeatures;
	changedFeatures.resize(set.mVotes.size());

	BOOST_FOREACH(const u256_int_pair& it, set.mVotes)
	{
		FeatureState& state = mFeatureMap[it.first];
		WriteLog (lsDEBUG, Features) << "Feature " << it.first.GetHex() << " has " << it.second << " votes, needs " << threshold;
		if (it.second >= threshold)
		{ // we have a majority
			state.mLastMajority = set.mCloseTime;
			if (state.mFirstMajority == 0)
			{
				WriteLog (lsWARNING, Features) << "Feature " << it.first << " attains a majority vote";
				state.mFirstMajority = set.mCloseTime;
				changedFeatures.push_back(it.first);
			}
		}
		else // we have no majority
		{
			if (state.mFirstMajority != 0)
			{
				WriteLog (lsWARNING, Features) << "Feature " << it.first << " loses majority vote";
				state.mFirstMajority = 0;
				state.mLastMajority = 0;
				changedFeatures.push_back(it.first);
			}
		}
	}
	mLastReport = set.mCloseTime;

	if (!changedFeatures.empty())
	{
		// WRITEME write changed features to SQL db
	}
}

void Features::setEnabledFeatures(const std::vector<uint256>& features)
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

void Features::setSupportedFeatures(const std::vector<uint256>& features)
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

void Features::doValidation(Ledger::ref lastClosedLedger, STObject& baseValidation)
{
	featureList_t lFeatures = getDesiredFeatures();
	if (lFeatures.empty())
		return;

	STVector256 vFeatures(sfFeatures);
	BOOST_FOREACH(const uint256& uFeature, lFeatures)
	{
		vFeatures.addValue(uFeature);
	}
	vFeatures.sort();
	baseValidation.setFieldV256(sfFeatures, vFeatures);
}

void Features::doVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition)
{
	featureList_t lFeatures = getFeaturesToEnable(lastClosedLedger->getCloseTimeNC());
	if (lFeatures.empty())
		return;

	BOOST_FOREACH(const uint256& uFeature, lFeatures)
	{
		WriteLog (lsWARNING, Features) << "We are voting for feature " << uFeature;
		SerializedTransaction trans(ttFEATURE);
		trans.setFieldAccount(sfAccount, uint160());
		trans.setFieldH256(sfFeature, uFeature);
		uint256 txID = trans.getTransactionID();
		WriteLog (lsWARNING, Features) << "Vote: " << txID;

		Serializer s;
		trans.add(s, true);

		SHAMapItem::pointer tItem = boost::make_shared<SHAMapItem>(txID, s.peekData());
		if (!initialPosition->addGiveItem(tItem, true, false))
		{
			WriteLog (lsWARNING, Features) << "Ledger already had feature transaction";
		}
	}
}

Json::Value Features::getJson(int)
{
	Json::Value ret(Json::objectValue);
	{
		boost::mutex::scoped_lock sl(mMutex);
		BOOST_FOREACH(const featureIt_t& it, mFeatureMap)
		{
			Json::Value v(Json::objectValue);

			v["supported"] = it.second.mSupported;

			if (it.second.mEnabled)
				v["enabled"] = true;
			else
			{
				v["enabled"] = false;
				if (mLastReport != 0)
				{
					if (it.second.mLastMajority == 0)
						v["majority"] = false;
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
				v["veto"] = true;

			ret[it.first.GetHex()] = v;
		}
	}

	return ret;
}

IFeatures* IFeatures::New (uint32 majorityTime, int majorityFraction)
{
    return new Features (majorityTime, majorityFraction);
}

// vim:ts=4
