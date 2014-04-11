//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

class Features;

SETUP_LOG (Features)

FeatureState* testFeature = NULL;

// VFALCO TODO Rename this to Features
class Features : public IFeatures
{
protected:

    typedef boost::unordered_map<uint256, FeatureState> featureMap_t;
    typedef std::pair<const uint256, FeatureState> featureIt_t;
    typedef boost::unordered_set<uint256> featureList_t;

    typedef RippleMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;

    featureMap_t    mFeatureMap;
    int             mMajorityTime;      // Seconds a feature must hold a majority
    int             mMajorityFraction;  // 256 = 100%
    uint32          mFirstReport;       // close time of first majority report
    uint32          mLastReport;        // close time of most recent majority report

    FeatureState*   getCreateFeature (uint256 const& feature, bool create);
    bool shouldEnable (uint32 closeTime, const FeatureState& fs);
    void setJson (Json::Value& v, const FeatureState&);

public:

    Features (uint32 majorityTime, int majorityFraction)
        : mLock (this, "Features", __FILE__, __LINE__)
        , mMajorityTime (majorityTime), mMajorityFraction (majorityFraction), mFirstReport (0), mLastReport (0)
    {
    }

    void addInitialFeatures ();

    FeatureState* addKnownFeature (const char* featureID, const char* friendlyName, bool veto);
    uint256 getFeature (const std::string& name);

    bool vetoFeature (uint256 const& feature);
    bool unVetoFeature (uint256 const& feature);

    bool enableFeature (uint256 const& feature);
    bool disableFeature (uint256 const& feature);

    bool isFeatureEnabled (uint256 const& feature);
    bool isFeatureSupported (uint256 const& feature);

    void setEnabledFeatures (const std::vector<uint256>& features);
    void setSupportedFeatures (const std::vector<uint256>& features);

    featureList_t getVetoedFeatures ();
    featureList_t getEnabledFeatures ();
    featureList_t getFeaturesToEnable (uint32 closeTime);   // gets features we would vote to enable
    featureList_t getDesiredFeatures ();                    // features we support, do not veto, are not enabled

    void reportValidations (const FeatureSet&);

    Json::Value getJson (int);
    Json::Value getJson (uint256 const& );

    void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation);
    void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition);
};

void Features::addInitialFeatures ()
{
    // For each feature this version supports, construct the FeatureState object by calling
    // getCreateFeature. Set any vetoes or defaults. A pointer to the FeatureState can be stashed

    testFeature = addKnownFeature ("1234", "testFeature", false);
}

FeatureState* Features::getCreateFeature (uint256 const& featureHash, bool create)
{
    // call with the mutex held
    featureMap_t::iterator it = mFeatureMap.find (featureHash);

    if (it == mFeatureMap.end ())
    {
        if (!create)
            return NULL;

        FeatureState* feature = & (mFeatureMap[featureHash]);

        {
            std::string query = "SELECT FirstMajority,LastMajority FROM Features WHERE hash='";
            query.append (featureHash.GetHex ());
            query.append ("';");

            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
            Database* db = getApp().getWalletDB ()->getDB ();

            if (db->executeSQL (query) && db->startIterRows ())
            {
                feature->mFirstMajority = db->getBigInt ("FirstMajority");
                feature->mLastMajority = db->getBigInt ("LastMajority");
                db->endIterRows ();
            }
        }

        return feature;
    }

    return & (it->second);
}

uint256 Features::getFeature (const std::string& name)
{
    if (!name.empty ())
    {
        BOOST_FOREACH (featureMap_t::value_type & it, mFeatureMap)
        {
            if (name == it.second.mFriendlyName)
                return it.first;
        }
    }

    return uint256 ();
}

FeatureState* Features::addKnownFeature (const char* featureID, const char* friendlyName, bool veto)
{
    uint256 hash;
    hash.SetHex (featureID);

    if (hash.isZero ())
    {
        assert (false);
        return NULL;
    }

    FeatureState* f = getCreateFeature (hash, true);

    if (friendlyName != NULL)
        f->setFriendlyName (friendlyName);

    f->mVetoed = veto;
    f->mSupported = true;

    return f;
}

bool Features::vetoFeature (uint256 const& feature)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    FeatureState* s = getCreateFeature (feature, true);

    if (s->mVetoed)
        return false;

    s->mVetoed = true;
    return true;
}

bool Features::unVetoFeature (uint256 const& feature)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    FeatureState* s = getCreateFeature (feature, false);

    if (!s || !s->mVetoed)
        return false;

    s->mVetoed = false;
    return true;
}

bool Features::enableFeature (uint256 const& feature)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    FeatureState* s = getCreateFeature (feature, true);

    if (s->mEnabled)
        return false;

    s->mEnabled = true;
    return true;
}

bool Features::disableFeature (uint256 const& feature)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    FeatureState* s = getCreateFeature (feature, false);

    if (!s || !s->mEnabled)
        return false;

    s->mEnabled = false;
    return true;
}

bool Features::isFeatureEnabled (uint256 const& feature)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    FeatureState* s = getCreateFeature (feature, false);
    return s && s->mEnabled;
}

bool Features::isFeatureSupported (uint256 const& feature)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    FeatureState* s = getCreateFeature (feature, false);
    return s && s->mSupported;
}

Features::featureList_t Features::getVetoedFeatures ()
{
    featureList_t ret;
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    BOOST_FOREACH (const featureIt_t & it, mFeatureMap)
    {
        if (it.second.mVetoed)
            ret.insert (it.first);
    }
    return ret;
}

Features::featureList_t Features::getEnabledFeatures ()
{
    featureList_t ret;
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    BOOST_FOREACH (const featureIt_t & it, mFeatureMap)
    {
        if (it.second.mEnabled)
            ret.insert (it.first);
    }
    return ret;
}

bool Features::shouldEnable (uint32 closeTime, const FeatureState& fs)
{
    if (fs.mVetoed || fs.mEnabled || !fs.mSupported || (fs.mLastMajority != mLastReport))
        return false;

    if (fs.mFirstMajority == mFirstReport)
    {
        // had a majority when we first started the server, relaxed check
        // WRITEME
    }

    // didn't have a majority when we first started the server, normal check
    return (fs.mLastMajority - fs.mFirstMajority) > mMajorityTime;

}

Features::featureList_t Features::getFeaturesToEnable (uint32 closeTime)
{
    featureList_t ret;
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (mLastReport != 0)
    {
        BOOST_FOREACH (const featureIt_t & it, mFeatureMap)
        {
            if (shouldEnable (closeTime, it.second))
                ret.insert (it.first);
        }
    }

    return ret;
}

Features::featureList_t Features::getDesiredFeatures ()
{
    featureList_t ret;
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    BOOST_FOREACH (const featureIt_t & it, mFeatureMap)
    {
        if (it.second.mSupported && !it.second.mEnabled && !it.second.mVetoed)
            ret.insert (it.first);
    }
    return ret;
}

void Features::reportValidations (const FeatureSet& set)
{
    if (set.mTrustedValidations == 0)
        return;

    int threshold = (set.mTrustedValidations * mMajorityFraction) / 256;

    typedef std::map<uint256, int>::value_type u256_int_pair;

    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (mFirstReport == 0)
        mFirstReport = set.mCloseTime;

    std::vector<uint256> changedFeatures;
    changedFeatures.resize (set.mVotes.size ());

    BOOST_FOREACH (const u256_int_pair & it, set.mVotes)
    {
        FeatureState& state = mFeatureMap[it.first];
        WriteLog (lsDEBUG, Features) << "Feature " << it.first.GetHex () << " has " << it.second << " votes, needs " << threshold;

        if (it.second >= threshold)
        {
            // we have a majority
            state.mLastMajority = set.mCloseTime;

            if (state.mFirstMajority == 0)
            {
                WriteLog (lsWARNING, Features) << "Feature " << it.first << " attains a majority vote";
                state.mFirstMajority = set.mCloseTime;
                changedFeatures.push_back (it.first);
            }
        }
        else // we have no majority
        {
            if (state.mFirstMajority != 0)
            {
                WriteLog (lsWARNING, Features) << "Feature " << it.first << " loses majority vote";
                state.mFirstMajority = 0;
                state.mLastMajority = 0;
                changedFeatures.push_back (it.first);
            }
        }
    }
    mLastReport = set.mCloseTime;

    if (!changedFeatures.empty ())
    {
        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
        Database* db = getApp().getWalletDB ()->getDB ();

        db->executeSQL ("BEGIN TRANSACTION;");
        BOOST_FOREACH (uint256 const & hash, changedFeatures)
        {
            FeatureState& fState = mFeatureMap[hash];
            db->executeSQL (boost::str (boost::format (
                                            "UPDATE Features SET FirstMajority = %d WHERE Hash = '%s';"
                                        ) % fState.mFirstMajority % hash.GetHex ()));
            db->executeSQL (boost::str (boost::format (
                                            "UPDATE Features SET LastMajority = %d WHERE Hash = '%s';"
                                        ) % fState.mLastMajority % hash.GetHex ()));
        }
        db->executeSQL ("END TRANSACTION;");
        changedFeatures.clear ();
    }
}

void Features::setEnabledFeatures (const std::vector<uint256>& features)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    BOOST_FOREACH (featureIt_t & it, mFeatureMap)
    {
        it.second.mEnabled = false;
    }
    BOOST_FOREACH (uint256 const & it, features)
    {
        mFeatureMap[it].mEnabled = true;
    }
}

void Features::setSupportedFeatures (const std::vector<uint256>& features)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    BOOST_FOREACH (featureIt_t & it, mFeatureMap)
    {
        it.second.mSupported = false;
    }
    BOOST_FOREACH (uint256 const & it, features)
    {
        mFeatureMap[it].mSupported = true;
    }
}

void Features::doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation)
{
    featureList_t lFeatures = getDesiredFeatures ();

    if (lFeatures.empty ())
        return;

    STVector256 vFeatures (sfFeatures);
    BOOST_FOREACH (uint256 const & uFeature, lFeatures)
    {
        vFeatures.addValue (uFeature);
    }
    vFeatures.sort ();
    baseValidation.setFieldV256 (sfFeatures, vFeatures);
}

void Features::doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition)
{
    featureList_t lFeatures = getFeaturesToEnable (lastClosedLedger->getCloseTimeNC ());

    if (lFeatures.empty ())
        return;

    BOOST_FOREACH (uint256 const & uFeature, lFeatures)
    {
        WriteLog (lsWARNING, Features) << "Voting for feature: " << uFeature;
        SerializedTransaction trans (ttFEATURE);
        trans.setFieldAccount (sfAccount, uint160 ());
        trans.setFieldH256 (sfFeature, uFeature);
        uint256 txID = trans.getTransactionID ();
        WriteLog (lsWARNING, Features) << "Vote ID: " << txID;

        Serializer s;
        trans.add (s, true);

        SHAMapItem::pointer tItem = boost::make_shared<SHAMapItem> (txID, s.peekData ());

        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            WriteLog (lsWARNING, Features) << "Ledger already had feature transaction";
        }
    }
}

Json::Value Features::getJson (int)
{
    Json::Value ret (Json::objectValue);
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        BOOST_FOREACH (const featureIt_t & it, mFeatureMap)
        {
            setJson (ret[it.first.GetHex ()] = Json::objectValue, it.second);
        }
    }
    return ret;
}

void Features::setJson (Json::Value& v, const FeatureState& fs)
{
    if (!fs.mFriendlyName.empty ())
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

Json::Value Features::getJson (uint256 const& feature)
{
    Json::Value ret = Json::objectValue;
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    setJson (ret[feature.GetHex ()] = Json::objectValue, *getCreateFeature (feature, true));
    return ret;
}

IFeatures* IFeatures::New (uint32 majorityTime, int majorityFraction)
{
    return new Features (majorityTime, majorityFraction);
}

// vim:ts=4
