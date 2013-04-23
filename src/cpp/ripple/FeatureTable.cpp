#include "FeatureTable.h"

#include <boost/foreach.hpp>

#include "Log.h"
#include "Application.h"
#include "ValidationCollection.h"
#include "HashPrefixes.h"

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

template<typename INT> class VotableInteger
{
protected:
	INT						mCurrent;		// The current setting
	INT						mTarget;		// The setting we want
	std::map<INT, int>		mVoteMap;

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
};

void FeeVote::doValidation(Ledger::ref lastClosedLedger, STObject& validation)
{
	if (lastClosedLedger->getBaseFee() != mTargetBaseFee)
	{
		cLog(lsINFO) << "Voting for base fee of " << mTargetBaseFee;
		validation.setFieldU64(sfBaseFee, mTargetBaseFee);
	}

	if (lastClosedLedger->getReserve(0) != mTargetReserveBase)
	{
		cLog(lsINFO) << "Voting for base resrve of " << mTargetReserveBase;
		validation.setFieldU32(sfReserveBase, mTargetReserveBase);
	}

	if (lastClosedLedger->getReserveInc() != mTargetReserveIncrement)
	{
		cLog(lsINFO) << "Voting for reserve increment of " << mTargetReserveIncrement;
		validation.setFieldU32(sfReserveIncrement, mTargetReserveIncrement);
	}
}

void FeeVote::doFeeVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition)
{
	// LCL must be flag ledger
	assert((lastClosedLedger->getLedgerSeq() % 256) == 0);

	VotableInteger<uint64> baseFeeVote(lastClosedLedger->getBaseFee(), mTargetBaseFee);
	VotableInteger<uint32> baseReserveVote(lastClosedLedger->getReserve(0), mTargetReserveBase);
	VotableInteger<uint32> incReserveVote(lastClosedLedger->getReserveInc(), mTargetReserveIncrement);

	// get validations for ledger before flag
	ValidationSet set = theApp->getValidations().getValidations(lastClosedLedger->getParentHash());
	BOOST_FOREACH(ValidationSet::value_type& value, set)
	{
		SerializedValidation& val = *value.second;
		if (val.isTrusted())
		{
			if (val.isFieldPresent(sfBaseFee))
				baseFeeVote.addVote(val.getFieldU64(sfBaseFee));
			else
				baseFeeVote.noVote();
			if (val.isFieldPresent(sfReserveBase))
				baseReserveVote.addVote(val.getFieldU32(sfReserveBase));
			else
				baseReserveVote.noVote();
			if (val.isFieldPresent(sfReserveIncrement))
				incReserveVote.addVote(val.getFieldU32(sfReserveIncrement));
			else
				incReserveVote.noVote();
		}
	}

	// choose our positions
	uint64 baseFee = baseFeeVote.getVotes();
	uint32 baseReserve = baseReserveVote.getVotes();
	uint32 incReserve = incReserveVote.getVotes();

	// add transactions to our position
	if ((baseFee != lastClosedLedger->getBaseFee()) ||
		(baseReserve != lastClosedLedger->getReserve(0)) ||
		(incReserve != lastClosedLedger->getReserveInc()))
	{
		cLog(lsWARNING) << "We are voting for a fee change: " << baseFee << "/" << baseReserve << "/" << incReserve;
		SerializedTransaction trans(ttFEE);
		trans.setFieldU64(sfBaseFee, baseFee);
		trans.setFieldU32(sfReferenceFeeUnits, 10);
		trans.setFieldU32(sfReserveBase, baseReserve);
		trans.setFieldU32(sfReserveIncrement, incReserve);


		Serializer s;
		s.add32(sHP_TransactionID);
		trans.add(s, true);
		uint256 txID = s.getSHA512Half();
		cLog(lsWARNING) << "Vote: " << txID;

		SHAMapItem::pointer tItem = boost::make_shared<SHAMapItem>(txID, s.peekData());
		if (!initialPosition->addGiveItem(tItem, true, false))
		{
			cLog(lsWARNING) << "Ledger already had fee change";
			}
	}
}

// vim:ts=4
