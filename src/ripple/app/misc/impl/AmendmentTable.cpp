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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/TxFlags.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <mutex>

namespace ripple {

static
std::vector<std::pair<uint256, std::string>>
parseSection (Section const& section)
{
    static boost::regex const re1 (
            "^"                       // start of line
            "(?:\\s*)"                // whitespace (optional)
            "([abcdefABCDEF0-9]{64})" // <hexadecimal amendment ID>
            "(?:\\s+)"                // whitespace
            "(\\S+)"                  // <description>
        , boost::regex_constants::optimize
    );

    std::vector<std::pair<uint256, std::string>> names;

    for (auto const& line : section.lines ())
    {
        boost::smatch match;

        if (!boost::regex_match (line, match, re1))
            Throw<std::runtime_error> (
                "Invalid entry '" + line +
                "' in [" + section.name () + "]");

        uint256 id;

        if (!id.SetHexExact (match[1]))
            Throw<std::runtime_error> (
                "Invalid amendment ID '" + match[1] +
                "' in [" + section.name () + "]");

        names.push_back (std::make_pair (id, match[2]));
    }

    return names;
}

/** Current state of an amendment.
    Tells if a amendment is supported, enabled or vetoed. A vetoed amendment
    means the node will never announce its support.
*/
struct AmendmentState
{
    /** If an amendment is vetoed, a server will not support it */
    bool vetoed = false;

    /** Indicates that the amendment has been enabled.
        This is a one-way switch: once an amendment is enabled
        it can never be disabled, but it can be superseded by
        a subsequent amendment.
    */
    bool enabled = false;

    /** Indicates an amendment that this server has code support for. */
    bool supported = false;

    /** The name of this amendment, possibly empty. */
    std::string name;

    explicit AmendmentState () = default;
};

/** The status of all amendments requested in a given window. */
struct AmendmentSet
{
private:
    // How many yes votes each amendment received
    hash_map<uint256, int> votes_;

public:
    // number of trusted validations
    int mTrustedValidations = 0;

    // number of votes needed
    int mThreshold = 0;

    AmendmentSet () = default;

    void tally (std::set<uint256> const& amendments)
    {
        ++mTrustedValidations;

        for (auto const& amendment : amendments)
            ++votes_[amendment];
    }

    int votes (uint256 const& amendment) const
    {
        auto const& it = votes_.find (amendment);

        if (it == votes_.end())
            return 0;

        return it->second;
    }
};

//------------------------------------------------------------------------------

/** Track the list of "amendments"

   An "amendment" is an option that can affect transaction processing rules.
   Amendments are proposed and then adopted or rejected by the network. An
   Amendment is uniquely identified by its AmendmentID, a 256-bit key.
*/
class AmendmentTableImpl final
    : public AmendmentTable
{
protected:
    std::mutex mutex_;

    hash_map<uint256, AmendmentState> amendmentMap_;
    std::uint32_t lastUpdateSeq_;

    // Time that an amendment must hold a majority for
    std::chrono::seconds const majorityTime_;

    // The amount of support that an amendment must receive
    // 0 = 0% and 256 = 100%
    int const majorityFraction_;

    // The results of the last voting round - may be empty if
    // we haven't participated in one yet.
    std::unique_ptr <AmendmentSet> lastVote_;

    // True if an unsupported amendment is enabled
    bool unsupportedEnabled_;

    beast::Journal j_;

    // Finds or creates state
    AmendmentState* add (uint256 const& amendment);

    // Finds existing state
    AmendmentState* get (uint256 const& amendment);

    void setJson (Json::Value& v, uint256 const& amendment, const AmendmentState&);

public:
    AmendmentTableImpl (
        std::chrono::seconds majorityTime,
        int majorityFraction,
        Section const& supported,
        Section const& enabled,
        Section const& vetoed,
        beast::Journal journal);

    uint256 find (std::string const& name) override;

    bool veto (uint256 const& amendment) override;
    bool unVeto (uint256 const& amendment) override;

    bool enable (uint256 const& amendment) override;
    bool disable (uint256 const& amendment) override;

    bool isEnabled (uint256 const& amendment) override;
    bool isSupported (uint256 const& amendment) override;

    bool hasUnsupportedEnabled () override;

    Json::Value getJson (int) override;
    Json::Value getJson (uint256 const&) override;

    bool needValidatedLedger (LedgerIndex seq) override;

    void doValidatedLedger (
        LedgerIndex seq,
        std::set<uint256> const& enabled) override;

    std::vector <uint256>
    doValidation (std::set<uint256> const& enabledAmendments) override;

    std::vector <uint256>
    getDesired () override;

    std::map <uint256, std::uint32_t>
    doVoting (
        NetClock::time_point closeTime,
        std::set<uint256> const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        std::vector<STValidation::pointer> const& validations) override;
};

//------------------------------------------------------------------------------

AmendmentTableImpl::AmendmentTableImpl (
        std::chrono::seconds majorityTime,
        int majorityFraction,
        Section const& supported,
        Section const& enabled,
        Section const& vetoed,
        beast::Journal journal)
    : lastUpdateSeq_ (0)
    , majorityTime_ (majorityTime)
    , majorityFraction_ (majorityFraction)
    , unsupportedEnabled_ (false)
    , j_ (journal)
{
    assert (majorityFraction_ != 0);

    std::lock_guard <std::mutex> sl (mutex_);

    for (auto const& a : parseSection(supported))
    {
        if (auto s = add (a.first))
        {
            JLOG (j_.debug()) <<
                "Amendment " << a.first << " is supported.";

            if (!a.second.empty ())
                s->name = a.second;

            s->supported = true;
        }
    }

    for (auto const& a : parseSection (enabled))
    {
        if (auto s = add (a.first))
        {
            JLOG (j_.debug()) <<
                "Amendment " << a.first << " is enabled.";

            if (!a.second.empty ())
                s->name = a.second;

            s->supported = true;
            s->enabled = true;
        }
    }

    for (auto const& a : parseSection (vetoed))
    {
        // Unknown amendments are effectively vetoed already
        if (auto s = get (a.first))
        {
            JLOG (j_.info()) <<
                "Amendment " << a.first << " is vetoed.";

            if (!a.second.empty ())
                s->name = a.second;

            s->vetoed = true;
        }
    }
}

AmendmentState*
AmendmentTableImpl::add (uint256 const& amendmentHash)
{
    // call with the mutex held
    return &amendmentMap_[amendmentHash];
}

AmendmentState*
AmendmentTableImpl::get (uint256 const& amendmentHash)
{
    // call with the mutex held
    auto ret = amendmentMap_.find (amendmentHash);

    if (ret == amendmentMap_.end())
        return nullptr;

    return &ret->second;
}

uint256
AmendmentTableImpl::find (std::string const& name)
{
    std::lock_guard <std::mutex> sl (mutex_);

    for (auto const& e : amendmentMap_)
    {
        if (name == e.second.name)
            return e.first;
    }

    return {};
}

bool
AmendmentTableImpl::veto (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mutex_);
    auto s = add (amendment);

    if (s->vetoed)
        return false;
    s->vetoed = true;
    return true;
}

bool
AmendmentTableImpl::unVeto (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mutex_);
    auto s = get (amendment);

    if (!s || !s->vetoed)
        return false;
    s->vetoed = false;
    return true;
}

bool
AmendmentTableImpl::enable (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mutex_);
    auto s = add (amendment);

    if (s->enabled)
        return false;

    s->enabled = true;

    if (! s->supported)
    {
        JLOG (j_.error()) <<
            "Unsupported amendment " << amendment << " activated.";
        unsupportedEnabled_ = true;
    }

    return true;
}

bool
AmendmentTableImpl::disable (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mutex_);
    auto s = get (amendment);

    if (!s || !s->enabled)
        return false;

    s->enabled = false;
    return true;
}

bool
AmendmentTableImpl::isEnabled (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mutex_);
    auto s = get (amendment);
    return s && s->enabled;
}

bool
AmendmentTableImpl::isSupported (uint256 const& amendment)
{
    std::lock_guard <std::mutex> sl (mutex_);
    auto s = get (amendment);
    return s && s->supported;
}

bool
AmendmentTableImpl::hasUnsupportedEnabled ()
{
    std::lock_guard <std::mutex> sl (mutex_);
    return unsupportedEnabled_;
}

std::vector <uint256>
AmendmentTableImpl::doValidation (
    std::set<uint256> const& enabled)
{
    // Get the list of amendments we support and do not
    // veto, but that are not already enabled
    std::vector <uint256> amendments;
    amendments.reserve (amendmentMap_.size());

    {
        std::lock_guard <std::mutex> sl (mutex_);
        for (auto const& e : amendmentMap_)
        {
            if (e.second.supported && ! e.second.vetoed &&
                (enabled.count (e.first) == 0))
            {
                amendments.push_back (e.first);
            }
        }
    }

    if (! amendments.empty())
        std::sort (amendments.begin (), amendments.end ());

    return amendments;
}

std::vector <uint256>
AmendmentTableImpl::getDesired ()
{
    // Get the list of amendments we support and do not veto
    return doValidation({});
}

std::map <uint256, std::uint32_t>
AmendmentTableImpl::doVoting (
    NetClock::time_point closeTime,
    std::set<uint256> const& enabledAmendments,
    majorityAmendments_t const& majorityAmendments,
    std::vector<STValidation::pointer> const& valSet)
{
    JLOG (j_.trace()) <<
        "voting at " << closeTime.time_since_epoch().count() <<
        ": " << enabledAmendments.size() <<
        ", " << majorityAmendments.size() <<
        ", " << valSet.size();

    auto vote = std::make_unique <AmendmentSet> ();

    // process validations for ledger before flag ledger
    for (auto const& val : valSet)
    {
        if (val->isTrusted ())
        {
            std::set<uint256> ballot;

            if (val->isFieldPresent (sfAmendments))
            {
                auto const choices =
                    val->getFieldV256 (sfAmendments);
                ballot.insert (choices.begin (), choices.end ());
            }

            vote->tally (ballot);
        }
    }

    vote->mThreshold = std::max(1,
        (vote->mTrustedValidations * majorityFraction_) / 256);

    JLOG (j_.debug()) <<
        "Received " << vote->mTrustedValidations <<
        " trusted validations, threshold is: " << vote->mThreshold;

    // Map of amendments to the action to be taken for each one. The action is
    // the value of the flags in the pseudo-transaction
    std::map <uint256, std::uint32_t> actions;

    {
        std::lock_guard <std::mutex> sl (mutex_);

        // process all amendments we know of
        for (auto const& entry : amendmentMap_)
        {
            NetClock::time_point majorityTime = {};

            bool const hasValMajority =
                (vote->votes (entry.first) >= vote->mThreshold);

            {
                auto const it = majorityAmendments.find (entry.first);
                if (it != majorityAmendments.end ())
                    majorityTime = it->second;
            }

            if (enabledAmendments.count (entry.first) != 0)
            {
                JLOG (j_.debug()) <<
                    entry.first << ": amendment already enabled";
            }
            else  if (hasValMajority &&
                (majorityTime == NetClock::time_point{}) &&
                ! entry.second.vetoed)
            {
                // Ledger says no majority, validators say yes
                JLOG (j_.debug()) <<
                    entry.first << ": amendment got majority";
                actions[entry.first] = tfGotMajority;
            }
            else if (! hasValMajority &&
                (majorityTime != NetClock::time_point{}))
            {
                // Ledger says majority, validators say no
                JLOG (j_.debug()) <<
                    entry.first << ": amendment lost majority";
                actions[entry.first] = tfLostMajority;
            }
            else if ((majorityTime != NetClock::time_point{}) &&
                ((majorityTime + majorityTime_) <= closeTime) &&
                ! entry.second.vetoed)
            {
                // Ledger says majority held
                JLOG (j_.debug()) <<
                    entry.first << ": amendment majority held";
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
    std::lock_guard <std::mutex> sl (mutex_);

    // Is there a ledger in which an amendment could have been enabled
    // between these two ledger sequences?

    return ((ledgerSeq - 1) / 256) != ((lastUpdateSeq_ - 1) / 256);
}

void
AmendmentTableImpl::doValidatedLedger (
    LedgerIndex ledgerSeq,
    std::set<uint256> const& enabled)
{
    for (auto& e : enabled)
        enable(e);
}

void
AmendmentTableImpl::setJson (Json::Value& v, const uint256& id, const AmendmentState& fs)
{
    if (!fs.name.empty())
        v[jss::name] = fs.name;

    v[jss::supported] = fs.supported;
    v[jss::vetoed] = fs.vetoed;
    v[jss::enabled] = fs.enabled;

    if (!fs.enabled && lastVote_)
    {
        auto const votesTotal = lastVote_->mTrustedValidations;
        auto const votesNeeded = lastVote_->mThreshold;
        auto const votesFor = lastVote_->votes (id);

        v[jss::count] = votesFor;
        v[jss::validations] = votesTotal;

        if (votesNeeded)
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
        std::lock_guard <std::mutex> sl(mutex_);
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
        std::lock_guard <std::mutex> sl(mutex_);
        auto a = add (amendmentID);
        setJson (jAmendment, amendmentID, *a);
    }

    return ret;
}

std::unique_ptr<AmendmentTable> make_AmendmentTable (
    std::chrono::seconds majorityTime,
    int majorityFraction,
    Section const& supported,
    Section const& enabled,
    Section const& vetoed,
    beast::Journal journal)
{
    return std::make_unique<AmendmentTableImpl> (
        majorityTime,
        majorityFraction,
        supported,
        enabled,
        vetoed,
        journal);
}

}  // ripple
