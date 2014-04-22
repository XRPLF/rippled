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

namespace ripple {

/** Track the list of "features"

    A "feature" is an option that can affect transaction processing
    rules that is identified by a 256-bit feature identifier
    and adopted, or rejected, by the network.
*/
class FeaturesImpl : public FeatureTable
{
protected:

    typedef ripple::unordered_map<uint256, FeatureState> featureMap_t;
    typedef std::pair<const uint256, FeatureState> featureIt_t;
    typedef boost::unordered_set<uint256> featureList_t;

    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    featureMap_t mFeatureMap;
    std::chrono::seconds m_majorityTime; // Seconds a feature must hold a majority
    int mMajorityFraction;  // 256 = 100%
    core::Clock::time_point m_firstReport; // close time of first majority report
    core::Clock::time_point m_lastReport;  // close time of most recent majority report
    beast::Journal m_journal;

    FeatureState* getCreate (uint256 const& feature, bool create);
    bool shouldEnable (std::uint32_t closeTime, const FeatureState& fs);
    void setJson (Json::Value& v, const FeatureState&);

public:

    FeaturesImpl (std::chrono::seconds majorityTime, int majorityFraction,
            beast::Journal journal)
        : m_majorityTime (majorityTime)
        , mMajorityFraction (majorityFraction)
        , m_firstReport (0)
        , m_lastReport (0)
        , m_journal (journal)
    {
    }

    void addInitial () override;

    FeatureState* addKnown (const char* featureID, const char* friendlyName,
        bool veto) override;
    uint256 get (const std::string& name) override;

    bool veto (uint256 const& feature) override;
    bool unVeto (uint256 const& feature) override;

    bool enable (uint256 const& feature) override;
    bool disable (uint256 const& feature) override;

    bool isEnabled (uint256 const& feature) override;
    bool isSupported (uint256 const& feature) override;

    void setEnabled (const std::vector<uint256>& features) override;
    void setSupported (const std::vector<uint256>& features) override;

    void reportValidations (const FeatureSet&) override;

    Json::Value getJson (int) override;
    Json::Value getJson (uint256 const&) override;

    void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) override;
    void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) override;

    featureList_t getVetoed ();
    featureList_t getEnabled ();
    featureList_t getToEnable (core::Clock::time_point closeTime);   // gets features we would vote to enable
    featureList_t getDesired ();    // features we support, do not veto, are not enabled
};

void
FeaturesImpl::addInitial ()
{
    // For each feature this version supports, construct the FeatureState object by calling
    // addKnown. Set any vetoes or defaults. A pointer to the FeatureState can be stashed
}

FeatureState*
FeaturesImpl::getCreate (uint256 const& featureHash, bool create)
{
    // call with the mutex held
    auto iter (mFeatureMap.find (featureHash));

    if (iter == mFeatureMap.end ())
    {
        if (!create)
            return nullptr;

        FeatureState* feature = & (mFeatureMap[featureHash]);

        {
            std::string query = "SELECT FirstMajority,LastMajority FROM Features WHERE hash='";
            query.append (featureHash.GetHex ());
            query.append ("';");

            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
            Database* db = getApp().getWalletDB ()->getDB ();

            if (db->executeSQL (query) && db->startIterRows ())
            {
                feature->m_firstMajority = db->getBigInt("FirstMajority");
                feature->m_lastMajority = db->getBigInt("LastMajority");
                db->endIterRows ();
            }
        }

        return feature;
    }

    return & (iter->second);
}

uint256
FeaturesImpl::get (const std::string& name)
{
    for (auto const& e : mFeatureMap)
    {
        if (name == e.second.mFriendlyName)
            return e.first;
    }

    return uint256 ();
}

FeatureState*
FeaturesImpl::addKnown (const char* featureID, const char* friendlyName,
    bool veto)
{
    uint256 hash;
    hash.SetHex (featureID);

    if (hash.isZero ())
    {
        assert (false);
        return nullptr;
    }

    FeatureState* f = getCreate (hash, true);

    if (friendlyName != nullptr)
        f->setFriendlyName (friendlyName);

    f->mVetoed = veto;
    f->mSupported = true;

    return f;
}

bool
FeaturesImpl::veto (uint256 const& feature)
{
    ScopedLockType sl (mLock);
    FeatureState* s = getCreate (feature, true);

    if (s->mVetoed)
        return false;

    s->mVetoed = true;
    return true;
}

bool
FeaturesImpl::unVeto (uint256 const& feature)
{
    ScopedLockType sl (mLock);
    FeatureState* s = getCreate (feature, false);

    if (!s || !s->mVetoed)
        return false;

    s->mVetoed = false;
    return true;
}

bool
FeaturesImpl::enable (uint256 const& feature)
{
    ScopedLockType sl (mLock);
    FeatureState* s = getCreate (feature, true);

    if (s->mEnabled)
        return false;

    s->mEnabled = true;
    return true;
}

bool
FeaturesImpl::disable (uint256 const& feature)
{
    ScopedLockType sl (mLock);
    FeatureState* s = getCreate (feature, false);

    if (!s || !s->mEnabled)
        return false;

    s->mEnabled = false;
    return true;
}

bool
FeaturesImpl::isEnabled (uint256 const& feature)
{
    ScopedLockType sl (mLock);
    FeatureState* s = getCreate (feature, false);
    return s && s->mEnabled;
}

bool
FeaturesImpl::isSupported (uint256 const& feature)
{
    ScopedLockType sl (mLock);
    FeatureState* s = getCreate (feature, false);
    return s && s->mSupported;
}

FeaturesImpl::featureList_t
FeaturesImpl::getVetoed ()
{
    featureList_t ret;
    ScopedLockType sl (mLock);
    for (auto const& e : mFeatureMap)
    {
        if (e.second.mVetoed)
            ret.insert (e.first);
    }
    return ret;
}

FeaturesImpl::featureList_t
FeaturesImpl::getEnabled ()
{
    featureList_t ret;
    ScopedLockType sl (mLock);
    for (auto const& e : mFeatureMap)
    {
        if (e.second.mEnabled)
            ret.insert (e.first);
    }
    return ret;
}

bool
FeaturesImpl::shouldEnable (std::uint32_t closeTime, const FeatureState& fs)
{
    if (fs.mVetoed || fs.mEnabled || !fs.mSupported || (fs.m_lastMajority != m_lastReport))
        return false;

    if (fs.m_firstMajority == m_firstReport)
    {
        // had a majority when we first started the server, relaxed check
        // WRITEME
    }

    // didn't have a majority when we first started the server, normal check
    return (fs.m_lastMajority - fs.m_firstMajority) > m_majorityTime.count();
}

FeaturesImpl::featureList_t
FeaturesImpl::getToEnable (core::Clock::time_point closeTime)
{
    featureList_t ret;
    ScopedLockType sl (mLock);

    if (m_lastReport != 0)
    {
        for (auto const& e : mFeatureMap)
        {
            if (shouldEnable (closeTime, e.second))
                ret.insert (e.first);
        }
    }

    return ret;
}

FeaturesImpl::featureList_t
FeaturesImpl::getDesired ()
{
    featureList_t ret;
    ScopedLockType sl (mLock);

    for (auto const& e : mFeatureMap)
    {
        if (e.second.mSupported && !e.second.mEnabled && !e.second.mVetoed)
            ret.insert (e.first);
    }

    return ret;
}

void
FeaturesImpl::reportValidations (const FeatureSet& set)
{
    if (set.mTrustedValidations == 0)
        return;

    int threshold = (set.mTrustedValidations * mMajorityFraction) / 256;

    typedef std::map<uint256, int>::value_type u256_int_pair;

    ScopedLockType sl (mLock);

    if (m_firstReport == 0)
        m_firstReport = set.mCloseTime;

    std::vector<uint256> changedFeatures;
    changedFeatures.resize (set.mVotes.size ());

    for (auto const& e : set.mVotes)
    {
        FeatureState& state = mFeatureMap[e.first];
        if (m_journal.debug) m_journal.debug <<
            "Feature " << e.first.GetHex () <<
            " has " << e.second <<
            " votes, needs " << threshold;

        if (e.second >= threshold)
        {
            // we have a majority
            state.m_lastMajority = set.mCloseTime;

            if (state.m_firstMajority == 0)
            {
                if (m_journal.warning) m_journal.warning <<
                    "Feature " << e.first <<
                    " attains a majority vote";

                state.m_firstMajority = set.mCloseTime;
                changedFeatures.push_back (e.first);
            }
        }
        else // we have no majority
        {
            if (state.m_firstMajority != 0)
            {
                if (m_journal.warning) m_journal.warning <<
                    "Feature " << e.first <<
                    " loses majority vote";

                state.m_firstMajority = 0;
                state.m_lastMajority = 0;
                changedFeatures.push_back (e.first);
            }
        }
    }
    m_lastReport = set.mCloseTime;

    if (!changedFeatures.empty ())
    {
        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
        Database* db = getApp().getWalletDB ()->getDB ();

        db->executeSQL ("BEGIN TRANSACTION;");
        for (auto const& hash : changedFeatures)
        {
            FeatureState& fState = mFeatureMap[hash];
            db->executeSQL (boost::str (boost::format (
                                            "UPDATE Features SET FirstMajority = %d WHERE Hash = '%s';"
                                        ) % fState.m_firstMajority % hash.GetHex ()));
            db->executeSQL (boost::str (boost::format (
                                            "UPDATE Features SET LastMajority = %d WHERE Hash = '%s';"
                                            ) % fState.m_lastMajority % hash.GetHex()));
        }
        db->executeSQL ("END TRANSACTION;");
        changedFeatures.clear ();
    }
}

void
FeaturesImpl::setEnabled (const std::vector<uint256>& features)
{
    ScopedLockType sl (mLock);
    for (auto& e : mFeatureMap)
    {
        e.second.mEnabled = false;
    }
    for (auto const& e : features)
    {
        mFeatureMap[e].mEnabled = true;
    }
}

void
FeaturesImpl::setSupported (const std::vector<uint256>& features)
{
    ScopedLockType sl (mLock);
    for (auto &e : mFeatureMap)
    {
        e.second.mSupported = false;
    }
    for (auto const& e : features)
    {
        mFeatureMap[e].mSupported = true;
    }
}

void
FeaturesImpl::doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation)
{
    featureList_t lFeatures = getDesired ();

    if (lFeatures.empty ())
        return;

    STVector256 vFeatures (sfFeatures);
    for (auto const& uFeature : lFeatures)
    {
        vFeatures.addValue (uFeature);
    }
    vFeatures.sort ();
    baseValidation.setFieldV256 (sfFeatures, vFeatures);
}

void
FeaturesImpl::doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition)
{

    // LCL must be flag ledger
    assert((lastClosedLedger->getLedgerSeq () % 256) == 0);

    FeatureSet featureSet (lastClosedLedger->getParentCloseTimeNC ());

    // get validations for ledger before flag ledger
    ValidationSet valSet = getApp().getValidations ().getValidations (lastClosedLedger->getParentHash ());
    for (auto const& entry : valSet)
    {
        auto const& val = *entry.second;

        if (val.isTrusted ())
        {
            featureSet.addVoter ();
            if (val.isFieldPresent (sfFeatures))
            {
                for (auto const& feature : val.getFieldV256 (sfFeatures))
                {
                    featureSet.addVote (feature);
                }
            }

        }
    }
    reportValidations (featureSet);

    featureList_t lFeatures = getToEnable (lastClosedLedger->getCloseTimeNC ());
    for (auto const& uFeature : lFeatures)
    {
        if (m_journal.warning) m_journal.warning <<
            "Voting for feature: " << uFeature;

        // Create the transaction to enable the feature
        SerializedTransaction trans (ttFEATURE);
        trans.setFieldAccount (sfAccount, uint160 ());
        trans.setFieldH256 (sfFeature, uFeature);
        uint256 txID = trans.getTransactionID ();

        if (m_journal.warning) m_journal.warning <<
            "Vote ID: " << txID;

        // Inject the transaction into our initial proposal
        Serializer s;
        trans.add (s, true);
#if RIPPLE_PROPOSE_FEATURES
        SHAMapItem::pointer tItem = boost::make_shared<SHAMapItem> (txID, s.peekData ());
        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            if (m_journal.warning) m_journal.warning <<
                "Ledger already had feature transaction";
        }
#endif
    }
}

Json::Value
FeaturesImpl::getJson (int)
{
    Json::Value ret(Json::objectValue);
    {
        ScopedLockType sl(mLock);
        for (auto const& e : mFeatureMap)
        {
            setJson (ret[e.first.GetHex ()] = Json::objectValue, e.second);
        }
    }
    return ret;
}

void
FeaturesImpl::setJson (Json::Value& v, const FeatureState& fs)
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

        if (m_lastReport != 0)
        {
            if (fs.m_lastMajority == 0)
            {
                v["majority"] = false;
            }
            else
            {
                if (fs.m_firstMajority != 0)
                {
                    if (fs.m_firstMajority == m_firstReport)
                        v["majority_start"] = "start";
                    else
                        v["majority_start"] = fs.m_firstMajority;
                }

                if (fs.m_lastMajority != 0)
                {
                    if (fs.m_lastMajority == m_lastReport)
                        v["majority_until"] = "now";
                    else
                        v["majority_until"] = fs.m_lastMajority;
                }
            }
        }
    }

    if (fs.mVetoed)
        v["veto"] = true;
}

Json::Value
FeaturesImpl::getJson (uint256 const& featureID)
{
    Json::Value ret = Json::objectValue;
    Json::Value& jFeature = (ret[featureID.GetHex()] = Json::objectValue);

    {
        ScopedLockType sl(mLock);

        FeatureState *featureState = getCreate (featureID, true);
        setJson (jFeature, *featureState);
    }

    return ret;
}

std::unique_ptr<FeatureTable>
make_FeatureTable (std::chrono::seconds majorityTime, int majorityFraction,
    beast::Journal journal)
{
    return std::make_unique<FeaturesImpl> (majorityTime, majorityFraction,
        journal);
}

} // ripple
