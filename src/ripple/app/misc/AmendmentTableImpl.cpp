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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/data/DatabaseCon.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/JsonFields.h>
#include <boost/format.hpp>
#include <boost/tokenizer.hpp>
#include <algorithm>

namespace ripple {
/** Track the list of "amendments"

   An "amendment" is an option that can affect transaction processing rules.
   Amendments are proposed and then adopted or rejected by the network. An
   Amendment is uniquely identified by its AmendmentID, a 256-bit key.
*/
template<class AppApiFacade>
class AmendmentTableImpl final : public AmendmentTable
{
protected:
    typedef hash_map<uint256, AmendmentState> amendmentMap_t;
    typedef hash_set<uint256> amendmentList_t;

    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    amendmentMap_t m_amendmentMap;
    std::chrono::seconds m_majorityTime; // Seconds an amendment must hold a majority
    int mMajorityFraction;  // 256 = 100%
    core::Clock::time_point m_firstReport; // close time of first majority report
    core::Clock::time_point m_lastReport;  // close time of most recent majority report
    beast::Journal m_journal;
    AppApiFacade m_appApiFacade;

    AmendmentState& getCreate (uint256 const& amendment);
    AmendmentState* getExisting (uint256 const& amendment);
    bool shouldEnable (std::uint32_t closeTime, const AmendmentState& fs);
    void setJson (Json::Value& v, const AmendmentState&);

public:
    AmendmentTableImpl (
        std::chrono::seconds majorityTime,
        int majorityFraction,
        beast::Journal journal)
        : m_majorityTime (majorityTime)
        , mMajorityFraction (majorityFraction)
        , m_firstReport (0)
        , m_lastReport (0)
        , m_journal (journal)
    {
    }

    void addInitial (Section const& section) override;

    void addKnown (AmendmentName const& name) override;

    uint256 get (std::string const& name) override;

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
    void doVoting (Ledger::ref lastClosedLedger,
                   std::shared_ptr<SHAMap> const& initialPosition) override;

    amendmentList_t getVetoed();
    amendmentList_t getEnabled();
    amendmentList_t getToEnable(core::Clock::time_point closeTime);   // gets amendments we would vote to enable
    amendmentList_t getDesired();    // amendments we support, do not veto, are not enabled
};

namespace detail
{
/** preEnabledAmendments is a static collection of amendments that are are
   enabled at build time.

   Add amendments to this collection at build time to enable them on this
   server.
*/

std::vector<AmendmentName> const preEnabledAmendments;
}

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::addInitial (Section const& section)
{
    for (auto const& a : detail::preEnabledAmendments)
    {
        if (!a.valid ())
        {
            std::string const errorMsg =
                (boost::format (
                     "preEnabledAmendments contains an invalid hash (expected "
                     "a hex number). Value was: %1%") %
                 a.hexString ()).str ();
            throw std::runtime_error (errorMsg);
        }
    }

    std::vector<AmendmentName> toAdd (detail::preEnabledAmendments);

    {
        // add the amendments from the config file
        int const numExpectedToks = 2;
        for (auto const& line : section.lines ())
        {
            boost::tokenizer<> tokenizer (line);
            std::vector<std::string> tokens (tokenizer.begin (),
                                             tokenizer.end ());
            if (tokens.size () != numExpectedToks)
            {
                std::string const errorMsg =
                    (boost::format (
                         "The %1% section in the config file expects %2% "
                         "items. Found %3%. Line was: %4%") %
                     SECTION_AMENDMENTS % numExpectedToks % tokens.size () %
                     line).str ();
                throw std::runtime_error (errorMsg);
            }

            toAdd.emplace_back (std::move (tokens[0]), std::move (tokens[1]));
            if (!toAdd.back ().valid ())
            {
                std::string const errorMsg =
                    (boost::format (
                         "%1% is not a valid hash. Expected a hex "
                         "number. In config setcion: %2%. Line was: "
                         "%3%") %
                     toAdd.back ().hexString () % SECTION_AMENDMENTS %
                     line).str ();
                throw std::runtime_error (errorMsg);
            }
        }
    }

    for (auto const& a : toAdd)
    {
        addKnown (a);
        enable (a.id ());
    }
}

template<class AppApiFacade>
AmendmentState&
AmendmentTableImpl<AppApiFacade>::getCreate (uint256 const& amendmentHash)
{
    // call with the mutex held
    auto iter (m_amendmentMap.find (amendmentHash));

    if (iter == m_amendmentMap.end())
    {
        AmendmentState& amendment = m_amendmentMap[amendmentHash];
        m_appApiFacade.setMajorityTimesFromDBToState (amendment, amendmentHash);
        return amendment;
    }

    return iter->second;
}

template<class AppApiFacade>
AmendmentState*
AmendmentTableImpl<AppApiFacade>::getExisting (uint256 const& amendmentHash)
{
    // call with the mutex held
    auto iter (m_amendmentMap.find (amendmentHash));

    if (iter == m_amendmentMap.end())
        return nullptr;

    return & (iter->second);
}

template<class AppApiFacade>
uint256
AmendmentTableImpl<AppApiFacade>::get (std::string const& name)
{
    ScopedLockType sl (mLock);

    for (auto const& e : m_amendmentMap)
    {
        if (name == e.second.mFriendlyName)
            return e.first;
    }

    return uint256 ();
}

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::addKnown (AmendmentName const& name)
{
    if (!name.valid ())
    {
        std::string const errorMsg =
            (boost::format (
                 "addKnown was given an invalid hash (expected a hex number). "
                 "Value was: %1%") %
             name.hexString ()).str ();
        throw std::runtime_error (errorMsg);
    }

    ScopedLockType sl (mLock);
    AmendmentState& amendment = getCreate (name.id ());

    if (!name.friendlyName ().empty ())
        amendment.setFriendlyName (name.friendlyName ());

    amendment.mVetoed = false;
    amendment.mSupported = true;
}

template<class AppApiFacade>
bool
AmendmentTableImpl<AppApiFacade>::veto (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState& s = getCreate (amendment);

    if (s.mVetoed)
        return false;

    s.mVetoed = true;
    return true;
}

template<class AppApiFacade>
bool
AmendmentTableImpl<AppApiFacade>::unVeto (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getExisting (amendment);

    if (!s || !s->mVetoed)
        return false;

    s->mVetoed = false;
    return true;
}

template<class AppApiFacade>
bool
AmendmentTableImpl<AppApiFacade>::enable (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState& s = getCreate (amendment);

    if (s.mEnabled)
        return false;

    s.mEnabled = true;
    return true;
}

template<class AppApiFacade>
bool
AmendmentTableImpl<AppApiFacade>::disable (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getExisting (amendment);

    if (!s || !s->mEnabled)
        return false;

    s->mEnabled = false;
    return true;
}

template<class AppApiFacade>
bool
AmendmentTableImpl<AppApiFacade>::isEnabled (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getExisting (amendment);
    return s && s->mEnabled;
}

template<class AppApiFacade>
bool
AmendmentTableImpl<AppApiFacade>::isSupported (uint256 const& amendment)
{
    ScopedLockType sl (mLock);
    AmendmentState* s = getExisting (amendment);
    return s && s->mSupported;
}

template<class AppApiFacade>
typename AmendmentTableImpl<AppApiFacade>::amendmentList_t
AmendmentTableImpl<AppApiFacade>::getVetoed ()
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

template<class AppApiFacade>
typename AmendmentTableImpl<AppApiFacade>::amendmentList_t
AmendmentTableImpl<AppApiFacade>::getEnabled ()
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

template<class AppApiFacade>
bool
AmendmentTableImpl<AppApiFacade>::shouldEnable (std::uint32_t closeTime,
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

template<class AppApiFacade>
typename AmendmentTableImpl<AppApiFacade>::amendmentList_t
AmendmentTableImpl<AppApiFacade>::getToEnable (core::Clock::time_point closeTime)
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

template<class AppApiFacade>
typename AmendmentTableImpl<AppApiFacade>::amendmentList_t
AmendmentTableImpl<AppApiFacade>::getDesired ()
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

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::reportValidations (const AmendmentSet& set)
{
    if (set.mTrustedValidations == 0)
        return;

    int threshold = (set.mTrustedValidations * mMajorityFraction) / 256;

    typedef std::map<uint256, int>::value_type u256_int_pair;

    ScopedLockType sl (mLock);

    if (m_firstReport == 0)
        m_firstReport = set.mCloseTime;

    std::vector<uint256> changedAmendments;
    changedAmendments.reserve (set.mVotes.size());

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
        m_appApiFacade.setMajorityTimesFromStateToDB (changedAmendments,
                                                      m_amendmentMap);
        changedAmendments.clear ();
    }
}

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::setEnabled (const std::vector<uint256>& amendments)
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

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::setSupported (const std::vector<uint256>& amendments)
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

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::doValidation (Ledger::ref lastClosedLedger,
    STObject& baseValidation)
{
    auto lAmendments = getDesired();

    if (lAmendments.empty())
        return;

    STVector256 amendments (sfAmendments);

    for (auto const& id : lAmendments)
        amendments.push_back (id);

    std::sort (amendments.begin (), amendments.end ());

    baseValidation.setFieldV256 (sfAmendments, amendments);
}

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::doVoting (Ledger::ref lastClosedLedger,
    std::shared_ptr<SHAMap> const& initialPosition)
{

    // LCL must be flag ledger
    assert((lastClosedLedger->getLedgerSeq () % 256) == 0);

    AmendmentSet amendmentSet (lastClosedLedger->getParentCloseTimeNC ());

    // get validations for ledger before flag ledger
    ValidationSet valSet = m_appApiFacade.getValidations (
        lastClosedLedger->getParentHash ());
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
        STTx trans (ttAMENDMENT);
        trans.setFieldAccount (sfAccount, Account ());
        trans.setFieldH256 (sfAmendment, uAmendment);
        uint256 txID = trans.getTransactionID ();

        if (m_journal.warning) m_journal.warning <<
            "Vote ID: " << txID;

        // Inject the transaction into our initial proposal
        Serializer s;
        trans.add (s, true);
#if RIPPLE_PROPOSE_AMENDMENTS
        auto tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());
        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            if (m_journal.warning) m_journal.warning <<
                "Ledger already had amendment transaction";
        }
#endif
    }
}

template<class AppApiFacade>
Json::Value
AmendmentTableImpl<AppApiFacade>::getJson (int)
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

template<class AppApiFacade>
void
AmendmentTableImpl<AppApiFacade>::setJson (Json::Value& v, const AmendmentState& fs)
{
    if (!fs.mFriendlyName.empty())
        v[jss::name] = fs.mFriendlyName;

    v[jss::supported] = fs.mSupported;
    v[jss::vetoed] = fs.mVetoed;

    if (fs.mEnabled)
        v[jss::enabled] = true;
    else
    {
        v[jss::enabled] = false;

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

template<class AppApiFacade>
Json::Value
AmendmentTableImpl<AppApiFacade>::getJson (uint256 const& amendmentID)
{
    Json::Value ret = Json::objectValue;
    Json::Value& jAmendment = (ret[to_string (amendmentID)] = Json::objectValue);

    {
        ScopedLockType sl(mLock);

        AmendmentState& amendmentState = getCreate (amendmentID);
        setJson (jAmendment, amendmentState);
    }

    return ret;
}

namespace detail
{
class AppApiFacadeImpl final
{
public:
    void setMajorityTimesFromDBToState (AmendmentState& toUpdate,
                                        uint256 const& amendmentHash) const;
    void setMajorityTimesFromStateToDB (
        std::vector<uint256> const& changedAmendments,
        hash_map<uint256, AmendmentState>& amendmentMap) const;
    ValidationSet getValidations (uint256 const& hash) const;
};

class AppApiFacadeMock final
{
public:
    void setMajorityTimesFromDBToState (AmendmentState& toUpdate,
                                        uint256 const& amendmentHash) const {};
    void setMajorityTimesFromStateToDB (
        std::vector<uint256> const& changedAmendments,
        hash_map<uint256, AmendmentState>& amendmentMap) const {};
    ValidationSet getValidations (uint256 const& hash) const
    {
        return ValidationSet ();
    };
};

void AppApiFacadeImpl::setMajorityTimesFromDBToState (
    AmendmentState& toUpdate,
    uint256 const& amendmentHash) const
{
    std::string query =
        "SELECT FirstMajority,LastMajority FROM Features WHERE hash='";
    query.append (to_string (amendmentHash));
    query.append ("';");

    auto db (getApp ().getWalletDB ().checkoutDb ());

    boost::optional<std::uint64_t> fm, lm;
    soci::statement st = (db->prepare << query,
                          soci::into(fm),
                          soci::into(lm));
    st.execute ();
    while (st.fetch ())
    {
        toUpdate.m_firstMajority = fm.value_or (0);
        toUpdate.m_lastMajority = lm.value_or (0);
    }
}

void AppApiFacadeImpl::setMajorityTimesFromStateToDB (
    std::vector<uint256> const& changedAmendments,
    hash_map<uint256, AmendmentState>& amendmentMap) const
{
    if (changedAmendments.empty ())
        return;

    auto db (getApp ().getWalletDB ().checkoutDb ());

    soci::transaction tr(*db);
    
    for (auto const& hash : changedAmendments)
    {
        AmendmentState const& fState = amendmentMap[hash];
        *db << boost::str (boost::format ("UPDATE Features SET FirstMajority "
                                          "= %d WHERE Hash = '%s';") %
                           fState.m_firstMajority % to_string (hash));
        *db << boost::str (boost::format ("UPDATE Features SET LastMajority "
                                          "= %d WHERE Hash = '%s';") %
                           fState.m_lastMajority % to_string (hash));
    }

    tr.commit ();
}

ValidationSet AppApiFacadeImpl::getValidations (uint256 const& hash) const
{
    return getApp ().getValidations ().getValidations (hash);
}
}  // detail

std::unique_ptr<AmendmentTable> make_AmendmentTable (
    std::chrono::seconds majorityTime,
    int majorityFraction,
    beast::Journal journal,
    bool useMockFacade)
{
    if (useMockFacade)
    {
        return std::make_unique<AmendmentTableImpl<detail::AppApiFacadeMock>>(
            majorityTime, majorityFraction, std::move (journal));
    }

    return std::make_unique<AmendmentTableImpl<detail::AppApiFacadeImpl>>(
        majorityTime, majorityFraction, std::move (journal));
}

}  // ripple
