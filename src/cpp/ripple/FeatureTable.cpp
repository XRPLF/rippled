
SETUP_LOG (FeatureTable)

FeatureState* testFeature = NULL;

void FeatureTable::addInitialFeatures()
{
	// For each feature this version supports, construct the FeatureState object by calling
	// getCreateFeature. Set any vetoes or defaults. A pointer to the FeatureState can be stashed

	testFeature = addKnownFeature("1234", "testFeature", false);
}

FeatureState* FeatureTable::getCreateFeature(const uint256& featureHash, bool create)
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

uint256 FeatureTable::getFeature(const std::string& name)
{
	if (!name.empty())
	{
		BOOST_FOREACH(featureMap_t::value_type& it, mFeatureMap)
		{
			if (name == it.second.mFriendlyName)
				return it.first;
		}
	}
	return uint256();
}

FeatureState* FeatureTable::addKnownFeature(const char *featureID, const char *friendlyName, bool veto)
{
	uint256 hash;
	hash.SetHex(featureID);
	if (hash.isZero())
	{
		assert(false);
		return NULL;
	}
	FeatureState* f = getCreateFeature(hash, true);

	if (friendlyName != NULL)
		f->setFriendlyName(friendlyName);

	f->mVetoed = veto;
	f->mSupported = true;

	return f;
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

bool FeatureTable::isFeatureSupported(const uint256& feature)
{
	boost::mutex::scoped_lock sl(mMutex);
	FeatureState *s = getCreateFeature(feature, false);
	return s && s->mSupported;
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
	// didn't have a majority when we first started the server, normal check
	return (fs.mLastMajority - fs.mFirstMajority) > mMajorityTime;

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

FeatureTable::featureList_t FeatureTable::getDesiredFeatures()
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

void FeatureTable::reportValidations(const FeatureSet& set)
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
		WriteLog (lsDEBUG, FeatureTable) << "Feature " << it.first.GetHex() << " has " << it.second << " votes, needs " << threshold;
		if (it.second >= threshold)
		{ // we have a majority
			state.mLastMajority = set.mCloseTime;
			if (state.mFirstMajority == 0)
			{
				WriteLog (lsWARNING, FeatureTable) << "Feature " << it.first << " attains a majority vote";
				state.mFirstMajority = set.mCloseTime;
				changedFeatures.push_back(it.first);
			}
		}
		else // we have no majority
		{
			if (state.mFirstMajority != 0)
			{
				WriteLog (lsWARNING, FeatureTable) << "Feature " << it.first << " loses majority vote";
				state.mFirstMajority = 0;
				state.mLastMajority = 0;
				changedFeatures.push_back(it.first);
			}
		}
	}
	mLastReport = set.mCloseTime;

	if (!changedFeatures.empty())
	{
		ScopedLock sl(theApp->getWalletDB()->getDBLock());
		Database* db = theApp->getWalletDB()->getDB();

		db->executeSQL("BEGIN TRANSACTION;");
		BOOST_FOREACH(const uint256& hash, changedFeatures)
		{
			FeatureState& fState = mFeatureMap[hash];
			db->executeSQL(boost::str(boost::format(
				"UPDATE Features SET FirstMajority = %d WHERE Hash = '%s';"
				) % fState.mFirstMajority % hash.GetHex()));
			db->executeSQL(boost::str(boost::format(
				"UPDATE Features SET LastMajority = %d WHERE Hash = '%s';"
				) % fState.mLastMajority % hash.GetHex()));
		}
		db->executeSQL("END TRANSACTION;");
		changedFeatures.clear();
	}
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

void FeatureTable::doValidation(Ledger::ref lastClosedLedger, STObject& baseValidation)
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

void FeatureTable::doVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition)
{
	featureList_t lFeatures = getFeaturesToEnable(lastClosedLedger->getCloseTimeNC());
	if (lFeatures.empty())
		return;

	BOOST_FOREACH(const uint256& uFeature, lFeatures)
	{
		WriteLog (lsWARNING, FeatureTable) << "Voting for feature: " << uFeature;
		SerializedTransaction trans(ttFEATURE);
		trans.setFieldAccount(sfAccount, uint160());
		trans.setFieldH256(sfFeature, uFeature);
		uint256 txID = trans.getTransactionID();
		WriteLog (lsWARNING, FeatureTable) << "Vote ID: " << txID;

		Serializer s;
		trans.add(s, true);

		SHAMapItem::pointer tItem = boost::make_shared<SHAMapItem>(txID, s.peekData());
		if (!initialPosition->addGiveItem(tItem, true, false))
		{
			WriteLog (lsWARNING, FeatureTable) << "Ledger already had feature transaction";
		}
	}
}

Json::Value FeatureTable::getJson(int)
{
	Json::Value ret(Json::objectValue);
	{
		boost::mutex::scoped_lock sl(mMutex);
		BOOST_FOREACH(const featureIt_t& it, mFeatureMap)
		{
			setJson(ret[it.first.GetHex()] = Json::objectValue, it.second);
		}
	}
	return ret;
}

void FeatureTable::setJson(Json::Value& v, const FeatureState& fs)
{
	if (!fs.mFriendlyName.empty())
		v["name"] = fs.mFriendlyName;

	v["supported"] = fs.mSupported;
	v["vetoed"] = fs.mVetoed;

	if (fs.mEnabled)
		v["enabled"] = true;
	else
	{
		v["enabled"] = false;
		if (mLastReport != 0)
		{
			if (fs.mLastMajority == 0)
			{
				v["majority"] = false;
			}
			else
			{
				if (fs.mFirstMajority != 0)
				{
					if (fs.mFirstMajority == mFirstReport)
						v["majority_start"] = "start";
					else
						v["majority_start"] = fs.mFirstMajority;
				}
				if (fs.mLastMajority != 0)
				{
					if (fs.mLastMajority == mLastReport)
						v["majority_until"] = "now";
					else
						v["majority_until"] = fs.mLastMajority;
				}
			}
		}
	}

	if (fs.mVetoed)
		v["veto"] = true;
}

Json::Value FeatureTable::getJson(const uint256& feature)
{
	Json::Value ret = Json::objectValue;
	boost::mutex::scoped_lock sl(mMutex);
	setJson(ret[feature.GetHex()] = Json::objectValue, *getCreateFeature(feature, true));
	return ret;
}

template<typename INT> class VotableInteger
{
public:
	VotableInteger(INT current, INT target) : mCurrent(current), mTarget(target)
	{
		++mVoteMap[mTarget];				// Add our vote
	}

	bool mayVote()
	{
		return mCurrent != mTarget;			// If we love the current setting, we will not vote
	}

	void addVote(INT vote)
	{
		++mVoteMap[vote];
	}

	void noVote()
	{
		addVote(mCurrent);
	}

	INT getVotes()
	{
		INT ourVote = mCurrent;
		int weight = 0;

		typedef typename std::map<INT, int>::value_type mapVType;
		BOOST_FOREACH(const mapVType& value, mVoteMap)
		{ // Take most voted value between current and target, inclusive
			// FIXME: Should take best value that can get a significant majority
			if ((value.first <= std::max(mTarget, mCurrent)) &&
				(value.first >= std::min(mTarget, mCurrent)) &&
				(value.second > weight))
			{
				ourVote = value.first;
				weight = value.second;
			}
		}

		return ourVote;
	}

private:
    INT						mCurrent;		// The current setting
	INT						mTarget;		// The setting we want
	std::map<INT, int>		mVoteMap;
};

// vim:ts=4
