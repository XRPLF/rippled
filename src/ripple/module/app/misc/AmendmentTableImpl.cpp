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

/** Track the list of "amendments"

    An "amendment" is an option that can affect transaction processing
    rules that is identified by a 256-bit amendment identifier
    and adopted, or rejected, by the network.
*/
    class AmendmentTableImpl : public AmendmentTable
{
protected:

    typedef ripple::unordered_map<uint256, AmendmentState> amendmentMap_t;
    typedef std::pair<const uint256, AmendmentState> amendmentIt_t;
    typedef boost::unordered_set<uint256> amendmentList_t;

    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    amendmentMap_t m_amendmentMap;
    std::chrono::seconds m_majorityTime; // Seconds an amendment must hold a majority
    int mMajorityFraction;  // 256 = 100%
    core::Clock::time_point m_firstReport; // close time of first majority report
    core::Clock::time_point m_lastReport;  // close time of most recent majority report
    beast::Journal m_journal;

    AmendmentState* getCreate (uint256 const& amendment, bool create);
    bool shouldEnable (std::uint32_t closeTime, const AmendmentState& fs);
    void setJson (Json::Value& v, const AmendmentState&);

public:

    AmendmentTableImpl (std::chrono::seconds majorityTime, int majorityFraction,
            beast::Journal journal)
        : m_majorityTime (majorityTime)
        , mMajorityFraction (majorityFraction)
        , m_firstReport (0)
        , m_lastReport (0)
        , m_journal (journal)
    {
    }

    void addInitial () override;

    AmendmentState* addKnown (const char* amendmentID, const char* friendlyName,
        bool veto) override;
    uint256 get (const std::string& name) override;

    bool veto (uint256 const& amendment) override;
    bool unVeto (uint256 const& amendment) override;

    bool enable (uint256 const& amendment) override;
    bool disable (uint256 const& amendment) override;

    bool isEnabled (uint256 const& amendment) override;
    bool isSupported (uint256 const& amendment) override;

    void setEnabled (const std::vector<uint256>& amendments) override;
    void setSupported (const std::vector<uint256>& amendments) override;

    void reportValidations (const AmendmentSet&) override;

    Json::Value getJson (int) override;
    Json::Value getJson (uint256 const&) override;

    void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) override;
    void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) override;

    amendmentList_t getVetoed();
    amendmentList_t getEnabled();
    amendmentList_t getToEnable(core::Clock::time_point closeTime);   // gets amendments we would vote to enable
    amendmentList_t getDesired();    // amendments we support, do not veto, are not enabled
};

void
AmendmentTableImpl::addInitial ()
{
    // For each amendment this version supports, construct the AmendmentState object by calling
    // addKnown. Set any vetoes or defaults. A pointer to the AmendmentState can be stashed
}

AmendmentState*
AmendmentTableImpl::getCreate (uint256 const& amendmentHash, bool create)
{
    // call with the mutex held
    auto iter (m_amendmentMap.find (amendmentHash));

    if (iter == m_amendmentMap.end())
    {
        if (!create)
            return nullptr;

        AmendmentState* amendment = & (m_amendmentMap[amendmentHash]);

        {
            std::string query = "SELECT FirstMajority,LastMajority FROM Features WHERE hash='";
            query.append (to_string (amendmentHash));
            query.append ("';");

            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
            Database* db = getApp().getWalletDB ()->getDB ();

            if (db->executeSQL (query) && db->startIterRows ())
            {
                amendment->m_firstMajority = db->getBigInt("FirstMajority");
                amendment->m_lastMajority = db->getBigInt("LastMajority");
                db->endIterRows ();
            }
        }

        return amendment;
    }

    return & (iter->second);
}

uint256
AmendmentTableImpl::get (const std::string& name)
{
    for (auto const& e : m_amendmentMap)
    {
        if (name == e.second.mFriendlyName)
            return e.first;
    }

    return uint256 ();
}

AmendmentState*
AmendmentTableImpl::addKnown (const char* amendmentID, const char* friendlyName,
    bool veto)
{
    uint256 hash;
    hash.SetHex (amendmentID);

    if (hash.isZero ())
    {
        assert (false);
        return nullptr;
    }

    AmendmentState* f = getCreate (hash, true);

    if (friendlyName != nullptr)
        f->setFriendlyName (friendlyName);

    f->mVetoed = veto;
    f->mSupported = true;

    return f;
}

bool
AmendmentTableImpl::veto (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getCreate (amendment, true);

    if (s->mVetoed)
        return false;

    s->mVetoed = true;
    return true;
}

bool
AmendmentTableImpl::unVeto (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getCreate (amendment, false);

    if (!s || !s->mVetoed)
        return false;

    s->mVetoed = false;
    return true;
}

bool
AmendmentTableImpl::enable (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getCreate (amendment, true);

    if (s->mEnabled)
        return false;

    s->mEnabled = true;
    return true;
}

bool
AmendmentTableImpl::disable (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getCreate (amendment, false);

    if (!s || !s->mEnabled)
        return false;

    s->mEnabled = false;
    return true;
}

bool
AmendmentTableImpl::isEnabled (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getCreate (amendment, false);
    return s && s->mEnabled;
}

bool
AmendmentTableImpl::isSupported (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getCreate (amendment, false);
    return s && s->mSupported;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getVetoed ()
{
    amendmentList_t ret;
    ScopedLockType sl (mLock);
    for (auto const& e : m_amendmentMap)
    {
        if (e.second.mVetoed)
            ret.insert (e.first);
    }
    return ret;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getEnabled ()
{
    amendmentList_t ret;
    ScopedLockType sl (mLock);
    for (auto const& e : m_amendmentMap)
    {
        if (e.second.mEnabled)
            ret.insert (e.first);
    }
    return ret;
}

bool
AmendmentTableImpl::shouldEnable (std::uint32_t closeTime,
    const AmendmentState& fs)
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

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getToEnable (core::Clock::time_point closeTime)
{
    amendmentList_t ret;
    ScopedLockType sl (mLock);

    if (m_lastReport != 0)
    {
        for (auto const& e : m_amendmentMap)
        {
            if (shouldEnable (closeTime, e.second))
                ret.insert (e.first);
        }
    }

    return ret;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getDesired ()
{
    amendmentList_t ret;
    ScopedLockType sl (mLock);

    for (auto const& e : m_amendmentMap)
    {
        if (e.second.mSupported && !e.second.mEnabled && !e.second.mVetoed)
            ret.insert (e.first);
    }

    return ret;
}

void
AmendmentTableImpl::reportValidations (const AmendmentSet& set)
{
    if (set.mTrustedValidations == 0)
        return;

    int threshold = (set.mTrustedValidations * mMajorityFraction) / 256;

    typedef std::map<uint256, int>::value_type u256_int_pair;

    ScopedLockType sl (mLock);

    if (m_firstReport == 0)
        m_firstReport = set.mCloseTime;

    std::vector<uint256> changedAmendments;
    changedAmendments.resize(set.mVotes.size());

    for (auto const& e : set.mVotes)
    {
        AmendmentState& state = m_amendmentMap[e.first];
        if (m_journal.debug) m_journal.debug <<
            "Amendment " << to_string (e.first) <<
            " has " << e.second <<
            " votes, needs " << threshold;

        if (e.second >= threshold)
        {
            // we have a majority
            state.m_lastMajority = set.mCloseTime;

            if (state.m_firstMajority == 0)
            {
                if (m_journal.warning) m_journal.warning <<
                    "Amendment " << to_string (e.first) <<
                    " attains a majority vote";

                state.m_firstMajority = set.mCloseTime;
                changedAmendments.push_back(e.first);
            }
        }
        else // we have no majority
        {
            if (state.m_firstMajority != 0)
            {
                if (m_journal.warning) m_journal.warning <<
                    "Amendment " << to_string (e.first) <<
                    " loses majority vote";

                state.m_firstMajority = 0;
                state.m_lastMajority = 0;
                changedAmendments.push_back(e.first);
            }
        }
    }
    m_lastReport = set.mCloseTime;

    if (!changedAmendments.empty())
    {
        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
        Database* db = getApp().getWalletDB ()->getDB ();

        db->executeSQL ("BEGIN TRANSACTION;");
        for (auto const& hash : changedAmendments)
        {
            AmendmentState& fState = m_amendmentMap[hash];
            db->executeSQL (boost::str (boost::format (
                "UPDATE Features SET FirstMajority = %d WHERE Hash = '%s';") % 
                fState.m_firstMajority % to_string (hash)));
            db->executeSQL (boost::str (boost::format (
                "UPDATE Features SET LastMajority = %d WHERE Hash = '%s';") % 
                fState.m_lastMajority % to_string(hash)));
        }
        db->executeSQL ("END TRANSACTION;");
        changedAmendments.clear();
    }
}

void
AmendmentTableImpl::setEnabled (const std::vector<uint256>& amendments)
{
    ScopedLockType sl (mLock);
    for (auto& e : m_amendmentMap)
    {
        e.second.mEnabled = false;
    }
    for (auto const& e : amendments)
    {
        m_amendmentMap[e].mEnabled = true;
    }
}

void
AmendmentTableImpl::setSupported (const std::vector<uint256>& amendments)
{
    ScopedLockType sl (mLock);
    for (auto &e : m_amendmentMap)
    {
        e.second.mSupported = false;
    }
    for (auto const& e : amendments)
    {
        m_amendmentMap[e].mSupported = true;
    }
}

void
AmendmentTableImpl::doValidation (Ledger::ref lastClosedLedger,
    STObject& baseValidation)
{
    amendmentList_t lAmendments = getDesired();

    if (lAmendments.empty())
        return;

    STVector256 vAmendments (sfAmendments);
    for (auto const& uAmendment : lAmendments)
    {
        vAmendments.addValue (uAmendment);
    }
    vAmendments.sort ();
    baseValidation.setFieldV256 (sfAmendments, vAmendments);
}

void
AmendmentTableImpl::doVoting (Ledger::ref lastClosedLedger,
    SHAMap::ref initialPosition)
{

    // LCL must be flag ledger
    assert((lastClosedLedger->getLedgerSeq () % 256) == 0);

    AmendmentSet amendmentSet (lastClosedLedger->getParentCloseTimeNC ());

    // get validations for ledger before flag ledger
    ValidationSet valSet = getApp().getValidations ().getValidations (lastClosedLedger->getParentHash ());
    for (auto const& entry : valSet)
    {
        auto const& val = *entry.second;

        if (val.isTrusted ())
        {
            amendmentSet.addVoter ();
            if (val.isFieldPresent (sfAmendments))
            {
                for (auto const& amendment : val.getFieldV256 (sfAmendments))
                {
                    amendmentSet.addVote (amendment);
                }
            }
        }
    }
    reportValidations (amendmentSet);

    amendmentList_t lAmendments = getToEnable (lastClosedLedger->getCloseTimeNC ());
    for (auto const& uAmendment : lAmendments)
    {
        if (m_journal.warning) m_journal.warning <<
            "Voting for amendment: " << uAmendment;

        // Create the transaction to enable the amendment
        SerializedTransaction trans (ttAMENDMENT);
        trans.setFieldAccount (sfAccount, uint160 ());
        trans.setFieldH256 (sfAmendment, uAmendment);
        uint256 txID = trans.getTransactionID ();

        if (m_journal.warning) m_journal.warning <<
            "Vote ID: " << txID;

        // Inject the transaction into our initial proposal
        Serializer s;
        trans.add (s, true);
#if RIPPLE_PROPOSE_AMENDMENTS
        SHAMapItem::pointer tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());
        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            if (m_journal.warning) m_journal.warning <<
                "Ledger already had amendment transaction";
        }
#endif
    }
}

Json::Value
AmendmentTableImpl::getJson (int)
{
    Json::Value ret(Json::objectValue);
    {
        ScopedLockType sl(mLock);
        for (auto const& e : m_amendmentMap)
        {
            setJson (ret[to_string (e.first)] = Json::objectValue, e.second);
        }
    }
    return ret;
}

void
AmendmentTableImpl::setJson (Json::Value& v, const AmendmentState& fs)
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
AmendmentTableImpl::getJson (uint256 const& amendmentID)
{
    Json::Value ret = Json::objectValue;
    Json::Value& jAmendment = (ret[to_string (amendmentID)] = Json::objectValue);

    {
        ScopedLockType sl(mLock);

        AmendmentState *amendmentState = getCreate (amendmentID, true);
        setJson (jAmendment, *amendmentState);
    }

    return ret;
}

std::unique_ptr<AmendmentTable>
make_AmendmentTable (std::chrono::seconds majorityTime, int majorityFraction,
    beast::Journal journal)
{
    return std::make_unique<AmendmentTableImpl> (majorityTime, majorityFraction,
        journal);
}

} // ripple
