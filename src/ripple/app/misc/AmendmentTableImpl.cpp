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
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/TxFlags.h>
#include <boost/format.hpp>
#include <boost/tokenizer.hpp>
#include <algorithm>
#include <mutex>

namespace ripple {

/** The status of all amendments requested in a given window. */
struct AmendmentSet
{
    std::uint32_t mCloseTime;

    // number of trusted validations
    int mTrustedValidations = 0;

    // number of votes needed
    int mThreshold = 0;

    // How many yes votes each amendment received
    hash_map<uint256, int> mVotes;

    AmendmentSet (std::uint32_t ct)
        : mCloseTime (ct)
    {
    }

    void addVoter ()
    {
        ++mTrustedValidations;
    }

    void addVote (uint256 const& amendment)
    {
        ++mVotes[amendment];
    }

    int count (uint256 const& amendment)
    {
        auto const& it = mVotes.find (amendment);
        return (it == mVotes.end()) ? 0 : it->second;
    }
};

/** Track the list of "amendments"

   An "amendment" is an option that can affect transaction processing rules.
   Amendments are proposed and then adopted or rejected by the network. An
   Amendment is uniquely identified by its AmendmentID, a 256-bit key.
*/
class AmendmentTableImpl final : public AmendmentTable
{
protected:
    using amendmentList_t = hash_set<uint256>;

    std::mutex mLock;

    hash_map<uint256, AmendmentState> amendmentMap_;
    std::uint32_t m_lastUpdateSeq;

    // Time that an amendment must hold a majority for
    std::chrono::seconds m_majorityTime;

    // The amount of support that an amendment must receive
    // 0 = 0% and 256 = 100%
    int mMajorityFraction;

    beast::Journal m_journal;

    // The results of the last voting round - may be empty if
    // we haven't participated in one yet.
    std::unique_ptr <AmendmentSet> lastVote_;

    AmendmentState& getCreate (uint256 const& amendment);
    AmendmentState* getExisting (uint256 const& amendment);
    void setJson (Json::Value& v, uint256 const& amendment, const AmendmentState&);

public:
    AmendmentTableImpl (
        std::chrono::seconds majorityTime,
        int majorityFraction,
        beast::Journal journal)
        : m_lastUpdateSeq (0)
        , m_majorityTime (majorityTime)
        , mMajorityFraction (majorityFraction)
        , m_journal (journal)
    {
        assert (mMajorityFraction != 0);
    }

    void addInitial (Section const& section) override;

    void addVetos (Section const& section) override;

    void addKnown (uint256 const& amendment, std::string name) override;

    uint256 get (std::string const& name) override;

    bool veto (uint256 const& amendment) override;
    bool unVeto (uint256 const& amendment) override;

    bool enable (uint256 const& amendment) override;
    bool disable (uint256 const& amendment) override;

    bool isEnabled (uint256 const& amendment) override;
    bool isSupported (uint256 const& amendment) override;

    void setEnabled (const std::vector<uint256>& amendments) override;
    void setSupported (const std::vector<uint256>& amendments) override;

    Json::Value getJson (int) override;
    Json::Value getJson (uint256 const&) override;

    bool needValidatedLedger (LedgerIndex seq) override;

    void doValidatedLedger (LedgerIndex seq,
        enabledAmendments_t enabled) override;

    std::vector <uint256>
    doValidation (enabledAmendments_t const& enabledAmendments) override;

    std::map <uint256, std::uint32_t>
    doVoting (
        std::uint32_t closeTime,
        enabledAmendments_t const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        ValidationSet const& validations) override;

    amendmentList_t getVetoed();
    amendmentList_t getEnabled();

    // Amendments we support, do not veto, and are not enabled
    amendmentList_t getDesired (enabledAmendments_t const&);
};

namespace detail
{
/** Amendments that this server supports and enables by default */
std::vector<std::pair<uint256, std::string>>
preEnabledAmendments ()
{
    return {};
}

/** Amendments that this server supports, but doesn't enable by default */
std::vector<std::pair<uint256, std::string>>
supportedAmendments ()
{
    return {};
}

std::vector<std::pair<uint256, std::string>>
parseSection (Section const& section)
{
    std::vector<std::pair<uint256, std::string>> names;
    int const numExpectedToks = 2;
    for (auto const& line : section.lines ())
    {
        boost::tokenizer<> tokenizer (line);
        std::vector<std::string> tokens (tokenizer.begin (), tokenizer.end ());

        if (tokens.size () != numExpectedToks)
            throw std::runtime_error (
                "Invalid entry in [" + section.name () + "]");

        uint256 id;
        if (!id.SetHexExact (tokens[0]))
            throw std::runtime_error (
                "Invalid amendment '" + tokens[0] +
                "' in [" + section.name () + "]");

        names.push_back (std::make_pair (id, tokens[1]));
    }

    return names;
}

}

void
AmendmentTableImpl::addInitial (Section const& section)
{
    for (auto const& a : detail::supportedAmendments ())
        addKnown (a.first, a.second);

    for (auto const& a : detail::preEnabledAmendments ())
    {
        addKnown (a.first, a.second);
        enable (a.first);
    }

    for (auto const& a : detail::parseSection (section))
    {
        addKnown (a.first, a.second);
        enable (a.first);
    }
}

void AmendmentTableImpl::addVetos (Section const& section)
{
    std::lock_guard <std::mutex> sl (mLock);
    for (auto const& n : detail::parseSection (section))
    {
        // Unknown amendments are effectively vetoed already
        auto const a = getExisting (n.first);
        if (a)
            a->mVetoed = true;
    }
}

AmendmentState&
AmendmentTableImpl::getCreate (uint256 const& amendmentHash)
{
    // call with the mutex held
    auto iter (amendmentMap_.find (amendmentHash));

    if (iter == amendmentMap_.end())
    {
        AmendmentState& amendment = amendmentMap_[amendmentHash];
        return amendment;
    }

    return iter->second;
}

AmendmentState*
AmendmentTableImpl::getExisting (uint256 const& amendmentHash)
{
    // call with the mutex held
    auto iter (amendmentMap_.find (amendmentHash));

    if (iter == amendmentMap_.end())
        return nullptr;

    return & (iter->second);
}

uint256
AmendmentTableImpl::get (std::string const& name)
{
    std::lock_guard <std::mutex> sl (mLock);

    for (auto const& e : amendmentMap_)
    {
        if (name == e.second.mFriendlyName)
            return e.first;
    }

    return uint256 ();
}

void
AmendmentTableImpl::addKnown (uint256 const& id, std::string name)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState& s = getCreate (id);

    if (!name.empty ())
        s.setFriendlyName (name);

    s.mVetoed = false;
    s.mSupported = true;
}

bool
AmendmentTableImpl::veto (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState& s = getCreate (amendment);

    if (s.mVetoed)
        return false;

    s.mVetoed = true;
    return true;
}

bool
AmendmentTableImpl::unVeto (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);

    if (!s || !s->mVetoed)
        return false;

    s->mVetoed = false;
    return true;
}

bool
AmendmentTableImpl::enable (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState& s = getCreate (amendment);

    if (s.mEnabled)
        return false;

    s.mEnabled = true;
    return true;
}

bool
AmendmentTableImpl::disable (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);

    if (!s || !s->mEnabled)
        return false;

    s->mEnabled = false;
    return true;
}

bool
AmendmentTableImpl::isEnabled (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);
    return s && s->mEnabled;
}

bool
AmendmentTableImpl::isSupported (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mLock);
    AmendmentState* s = getExisting (amendment);
    return s && s->mSupported;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getVetoed ()
{
    amendmentList_t ret;
    std::lock_guard <std::mutex> sl (mLock);
    for (auto const& e : amendmentMap_)
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
    std::lock_guard <std::mutex> sl (mLock);
    for (auto const& e : amendmentMap_)
    {
        if (e.second.mEnabled)
            ret.insert (e.first);
    }
    return ret;
}

AmendmentTableImpl::amendmentList_t
AmendmentTableImpl::getDesired (enabledAmendments_t const& enabled)
{
    amendmentList_t ret;
    std::lock_guard <std::mutex> sl (mLock);

    for (auto const& e : amendmentMap_)
    {
        if (e.second.mSupported && ! e.second.mVetoed &&
            (enabled.count (e.first) == 0))
            ret.insert (e.first);
    }

    return ret;
}

void
AmendmentTableImpl::setEnabled (const std::vector<uint256>& amendments)
{
    std::lock_guard <std::mutex> sl (mLock);
    for (auto& e : amendmentMap_)
    {
        e.second.mEnabled = false;
    }
    for (auto const& e : amendments)
    {
        amendmentMap_[e].mEnabled = true;
    }
}

void
AmendmentTableImpl::setSupported (const std::vector<uint256>& amendments)
{
    std::lock_guard <std::mutex> sl (mLock);
    for (auto &e : amendmentMap_)
    {
        e.second.mSupported = false;
    }
    for (auto const& e : amendments)
    {
        amendmentMap_[e].mSupported = true;
    }
}

std::vector <uint256>
AmendmentTableImpl::doValidation (
    enabledAmendments_t const& enabledAmendments)
{
    std::vector <uint256> amendments;
    for (auto const& id : getDesired (enabledAmendments))
        amendments.emplace_back (id);
    std::sort (amendments.begin (), amendments.end ());
    return amendments;
}

std::map <uint256, std::uint32_t>
AmendmentTableImpl::doVoting (
    std::uint32_t closeTime,
    enabledAmendments_t const& enabledAmendments,
    majorityAmendments_t const& majorityAmendments,
    ValidationSet const& valSet)
{
    // LCL must be flag ledger
    //assert((lastClosedLedger->info().seq % 256) == 0);

    auto vote = std::make_unique <AmendmentSet> (closeTime);

    // process validations for ledger before flag ledger
    for (auto const& entry : valSet)
    {
        auto const& val = *entry.second;

        if (val.isTrusted ())
        {
            vote->addVoter ();
            if (val.isFieldPresent (sfAmendments))
            {
                auto const amendments = val.getFieldV256 (sfAmendments);
                for (auto const& a : amendments)
                    vote->addVote (a);
            }
        }
    }

    vote->mThreshold = (vote->mTrustedValidations * mMajorityFraction) / 256;

    JLOG (m_journal.trace) <<
        "Validation threshold is: " << vote->mThreshold <<
        ", received " << vote->mTrustedValidations;

    // Map of amendments to the action to be taken
    // for each one. The action is the value of the
    // flags in the pseudo-transaction
    std::map <uint256, std::uint32_t> actions;

    {
        std::lock_guard <std::mutex> sl (mLock);

        // process all amendments we know of
        for (auto const& entry : amendmentMap_)
        {
            bool const hasValMajority =
                (vote->count (entry.first) >= vote->mThreshold);

            std::uint32_t majorityTime = 0;
            auto const it = majorityAmendments.find (entry.first);
            if (it != majorityAmendments.end ())
                majorityTime = it->second;

            // FIXME: Add logging here
            if (enabledAmendments.count (entry.first) != 0)
            {
                // Already enabled, nothing to do
            }
            else  if (hasValMajority && (majorityTime == 0) && (! entry.second.mVetoed))
            {
                // Ledger says no majority, validators say yes
                actions[entry.first] = tfGotMajority;
            }
            else if (! hasValMajority && (majorityTime != 0))
            {
                // Ledger says majority, validators say no
                actions[entry.first] = tfLostMajority;
            }
            else if ((majorityTime != 0) &&
                ((majorityTime + m_majorityTime.count()) <= closeTime) &&
                ! entry.second.mVetoed)
            {
                // Ledger says majority held
                actions[entry.first] = 0;
            }
        }

        // Stash for reporting
        lastVote_ = std::move(vote);
    }

    return actions;
}

bool
AmendmentTableImpl::needValidatedLedger (LedgerIndex ledgerSeq)
{
    std::lock_guard <std::mutex> sl (mLock);

    // Is there a ledger in which an amendment could have been enabled
    // between these two ledger sequences?

    return ((ledgerSeq - 1) / 256) != ((m_lastUpdateSeq - 1) / 256);
}

void
AmendmentTableImpl::doValidatedLedger (LedgerIndex ledgerSeq,
    enabledAmendments_t enabled)
{
    std::lock_guard <std::mutex> sl (mLock);

    for (auto& e : amendmentMap_)
        e.second.mEnabled = (enabled.count (e.first) != 0);
}

void
AmendmentTableImpl::setJson (Json::Value& v, const uint256& id, const AmendmentState& fs)
{
    if (!fs.mFriendlyName.empty())
        v[jss::name] = fs.mFriendlyName;

    v[jss::supported] = fs.mSupported;
    v[jss::vetoed] = fs.mVetoed;
    v[jss::enabled] = fs.mEnabled;

    if (!fs.mEnabled && lastVote_)
    {
        int votesTotal = 0, votesNeeded = 0, votesFor = 0;
        {
            votesTotal = lastVote_->mTrustedValidations;
            votesNeeded = lastVote_->mThreshold;
            auto j = lastVote_->mVotes.find (id);
            if (j != lastVote_->mVotes.end ())
                votesFor = j->second;
        }
        if (votesTotal != 0 && votesNeeded != 0)
        {
            v[jss::vote] = votesFor * 256 / votesNeeded;
            v[jss::threshold] = votesNeeded;
        }
    }
}

Json::Value
AmendmentTableImpl::getJson (int)
{
    Json::Value ret(Json::objectValue);
    {
        std::lock_guard <std::mutex> sl(mLock);
        for (auto const& e : amendmentMap_)
        {
            setJson (ret[to_string (e.first)] = Json::objectValue,
                e.first, e.second);
        }
    }
    return ret;
}

Json::Value
AmendmentTableImpl::getJson (uint256 const& amendmentID)
{
    Json::Value ret = Json::objectValue;
    Json::Value& jAmendment = (ret[to_string (amendmentID)] = Json::objectValue);

    {
        std::lock_guard <std::mutex> sl(mLock);

        AmendmentState& amendmentState = getCreate (amendmentID);
        setJson (jAmendment, amendmentID, amendmentState);
    }

    return ret;
}

std::unique_ptr<AmendmentTable> make_AmendmentTable (
    std::chrono::seconds majorityTime,
    int majorityFraction,
    beast::Journal journal)
{
    return std::make_unique<AmendmentTableImpl>(
        majorityTime, majorityFraction, journal);
}

}  // ripple
