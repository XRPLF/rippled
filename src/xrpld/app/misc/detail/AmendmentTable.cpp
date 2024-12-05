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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/rdb/Wallet.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <mutex>

namespace ripple {

static std::vector<std::pair<uint256, std::string>>
parseSection(Section const& section)
{
    static boost::regex const re1(
        "^"                        // start of line
        "(?:\\s*)"                 // whitespace (optional)
        "([abcdefABCDEF0-9]{64})"  // <hexadecimal amendment ID>
        "(?:\\s+)"                 // whitespace
        "(\\S+)"                   // <description>
        ,
        boost::regex_constants::optimize);

    std::vector<std::pair<uint256, std::string>> names;

    for (auto const& line : section.lines())
    {
        boost::smatch match;

        if (!boost::regex_match(line, match, re1))
            Throw<std::runtime_error>(
                "Invalid entry '" + line + "' in [" + section.name() + "]");

        uint256 id;

        if (!id.parseHex(match[1]))
            Throw<std::runtime_error>(
                "Invalid amendment ID '" + match[1] + "' in [" +
                section.name() + "]");

        names.push_back(std::make_pair(id, match[2]));
    }

    return names;
}

/** TrustedVotes records the most recent votes from trusted validators.
    We keep a record in an effort to avoid "flapping" while amendment voting
    is in process.

    If a trusted validator loses synchronization near a flag ledger their
    amendment votes may be lost during that round.  If the validator is a
    bit flaky, then this can cause an amendment to appear to repeatedly
    gain and lose support.

    TrustedVotes addresses the problem by holding on to the last vote seen
    from every trusted validator.  So if any given validator is off line near
    a flag ledger we can assume that they did not change their vote.

    If we haven't seen any STValidations from a validator for several hours we
    lose confidence that the validator hasn't changed their position.  So
    there's a timeout.  We remove upVotes if they haven't been updated in
    several hours.
*/
class TrustedVotes
{
private:
    static constexpr NetClock::time_point maxTimeout =
        NetClock::time_point::max();

    // Associates each trusted validator with the last votes we saw from them
    // and an expiration for that record.
    struct UpvotesAndTimeout
    {
        std::vector<uint256> upVotes;
        NetClock::time_point timeout = maxTimeout;
    };
    hash_map<PublicKey, UpvotesAndTimeout> recordedVotes_;

public:
    TrustedVotes() = default;
    TrustedVotes(TrustedVotes const& rhs) = delete;
    TrustedVotes&
    operator=(TrustedVotes const& rhs) = delete;

    // Called when the list of trusted validators changes.
    //
    // Call with AmendmentTable::mutex_ locked.
    void
    trustChanged(
        hash_set<PublicKey> const& allTrusted,
        std::lock_guard<std::mutex> const& lock)
    {
        decltype(recordedVotes_) newRecordedVotes;
        newRecordedVotes.reserve(allTrusted.size());

        // Make sure every PublicKey in allTrusted is represented in
        // recordedVotes_.  Also make sure recordedVotes_ contains
        // no additional PublicKeys.
        for (auto& trusted : allTrusted)
        {
            if (recordedVotes_.contains(trusted))
            {
                // Preserve this validator's previously saved voting state.
                newRecordedVotes.insert(recordedVotes_.extract(trusted));
            }
            else
            {
                // New validators have a starting position of no on everything.
                // Add the entry with an empty vector and maxTimeout.
                newRecordedVotes[trusted];
            }
        }
        // The votes of any no-longer-trusted validators will be destroyed
        // when changedTrustedVotes goes out of scope.
        recordedVotes_.swap(newRecordedVotes);
    }

    // Called when we receive the latest votes.
    //
    // Call with AmendmentTable::mutex_ locked.
    void
    recordVotes(
        Rules const& rules,
        std::vector<std::shared_ptr<STValidation>> const& valSet,
        NetClock::time_point const closeTime,
        std::lock_guard<std::mutex> const& lock)
    {
        // When we get an STValidation we save the upVotes it contains, but
        // we also set an expiration for those upVotes.  The following constant
        // controls the timeout.
        //
        // There really is no "best" timeout to choose for when we finally
        // lose confidence that we know how a validator is voting.  But part
        // of the point of recording validator votes is to avoid flapping of
        // amendment votes.  A 24h timeout says that we will change the local
        // record of a validator's vote to "no" 24h after the last vote seen
        // from that validator.  So flapping due to that validator being off
        // line will happen less frequently than every 24 hours.
        using namespace std::chrono_literals;
        static constexpr NetClock::duration expiresAfter = 24h;

        // Walk all validations and replace previous votes from trusted
        // validators with these newest votes.
        for (auto const& val : valSet)
        {
            // If this validation comes from one of our trusted validators...
            if (auto const iter = recordedVotes_.find(val->getSignerPublic());
                iter != recordedVotes_.end())
            {
                iter->second.timeout = closeTime + expiresAfter;
                if (val->isFieldPresent(sfAmendments))
                {
                    auto const& choices = val->getFieldV256(sfAmendments);
                    iter->second.upVotes.assign(choices.begin(), choices.end());
                }
                else
                {
                    // This validator does not upVote any amendments right now.
                    iter->second.upVotes.clear();
                }
            }
        }

        // Now remove any expired records from recordedVotes_.
        std::for_each(
            recordedVotes_.begin(),
            recordedVotes_.end(),
            [&closeTime](decltype(recordedVotes_)::value_type& votes) {
                if (closeTime > votes.second.timeout)
                {
                    votes.second.timeout = maxTimeout;
                    votes.second.upVotes.clear();
                }
            });
    }

    // Return the information needed by AmendmentSet to determine votes.
    //
    // Call with AmendmentTable::mutex_ locked.
    [[nodiscard]] std::pair<int, hash_map<uint256, int>>
    getVotes(Rules const& rules, std::lock_guard<std::mutex> const& lock) const
    {
        hash_map<uint256, int> ret;
        for (auto& validatorVotes : recordedVotes_)
        {
            for (uint256 const& amendment : validatorVotes.second.upVotes)
            {
                ret[amendment] += 1;
            }
        }
        return {recordedVotes_.size(), ret};
    }
};

/** Current state of an amendment.
    Tells if a amendment is supported, enabled or vetoed. A vetoed amendment
    means the node will never announce its support.
*/
struct AmendmentState
{
    /** If an amendment is down-voted, a server will not vote to enable it */
    AmendmentVote vote = AmendmentVote::down;

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

    explicit AmendmentState() = default;
};

/** The status of all amendments requested in a given window. */
class AmendmentSet
{
private:
    // How many yes votes each amendment received
    hash_map<uint256, int> votes_;
    Rules const& rules_;
    // number of trusted validations
    int trustedValidations_ = 0;
    // number of votes needed
    int threshold_ = 0;

    void
    computeThreshold(int trustedValidations, Rules const& rules)
    {
        threshold_ = !rules_.enabled(fixAmendmentMajorityCalc)
            ? std::max(
                  1L,
                  static_cast<long>(
                      (trustedValidations_ *
                       preFixAmendmentMajorityCalcThreshold.num) /
                      preFixAmendmentMajorityCalcThreshold.den))
            : std::max(
                  1L,
                  static_cast<long>(
                      (trustedValidations_ *
                       postFixAmendmentMajorityCalcThreshold.num) /
                      postFixAmendmentMajorityCalcThreshold.den));
    }

public:
    AmendmentSet(
        Rules const& rules,
        TrustedVotes const& trustedVotes,
        std::lock_guard<std::mutex> const& lock)
        : rules_(rules)
    {
        // process validations for ledger before flag ledger.
        auto [trustedCount, newVotes] = trustedVotes.getVotes(rules, lock);

        trustedValidations_ = trustedCount;
        votes_.swap(newVotes);

        computeThreshold(trustedValidations_, rules);
    }

    bool
    passes(uint256 const& amendment) const
    {
        auto const& it = votes_.find(amendment);

        if (it == votes_.end())
            return false;

        // Before this fix, it was possible for an amendment to activate with a
        // percentage slightly less than 80% because we compared for "greater
        // than or equal to" instead of strictly "greater than".
        // One validator is an exception, otherwise it is not possible
        // to gain majority.
        if (!rules_.enabled(fixAmendmentMajorityCalc) ||
            trustedValidations_ == 1)
            return it->second >= threshold_;

        return it->second > threshold_;
    }

    int
    votes(uint256 const& amendment) const
    {
        auto const& it = votes_.find(amendment);

        if (it == votes_.end())
            return 0;

        return it->second;
    }

    int
    trustedValidations() const
    {
        return trustedValidations_;
    }

    int
    threshold() const
    {
        return threshold_;
    }
};

//------------------------------------------------------------------------------

/** Track the list of "amendments"

   An "amendment" is an option that can affect transaction processing rules.
   Amendments are proposed and then adopted or rejected by the network. An
   Amendment is uniquely identified by its AmendmentID, a 256-bit key.
*/
class AmendmentTableImpl final : public AmendmentTable
{
private:
    mutable std::mutex mutex_;

    hash_map<uint256, AmendmentState> amendmentMap_;
    std::uint32_t lastUpdateSeq_;

    // Record of the last votes seen from trusted validators.
    TrustedVotes previousTrustedVotes_;

    // Time that an amendment must hold a majority for
    std::chrono::seconds const majorityTime_;

    // The results of the last voting round - may be empty if
    // we haven't participated in one yet.
    std::unique_ptr<AmendmentSet> lastVote_;

    // True if an unsupported amendment is enabled
    bool unsupportedEnabled_;

    // Unset if no unsupported amendments reach majority,
    // else set to the earliest time an unsupported amendment
    // will be enabled.
    std::optional<NetClock::time_point> firstUnsupportedExpected_;

    beast::Journal const j_;

    // Database which persists veto/unveto vote
    DatabaseCon& db_;

    // Finds or creates state.  Must be called with mutex_ locked.
    AmendmentState&
    add(uint256 const& amendment, std::lock_guard<std::mutex> const& lock);

    // Finds existing state.  Must be called with mutex_ locked.
    AmendmentState*
    get(uint256 const& amendment, std::lock_guard<std::mutex> const& lock);

    AmendmentState const*
    get(uint256 const& amendment,
        std::lock_guard<std::mutex> const& lock) const;

    // Injects amendment json into v.  Must be called with mutex_ locked.
    void
    injectJson(
        Json::Value& v,
        uint256 const& amendment,
        AmendmentState const& state,
        bool isAdmin,
        std::lock_guard<std::mutex> const& lock) const;

    void
    persistVote(
        uint256 const& amendment,
        std::string const& name,
        AmendmentVote vote) const;

public:
    AmendmentTableImpl(
        Application& app,
        std::chrono::seconds majorityTime,
        std::vector<FeatureInfo> const& supported,
        Section const& enabled,
        Section const& vetoed,
        beast::Journal journal);

    uint256
    find(std::string const& name) const override;

    bool
    veto(uint256 const& amendment) override;
    bool
    unVeto(uint256 const& amendment) override;

    bool
    enable(uint256 const& amendment) override;

    bool
    isEnabled(uint256 const& amendment) const override;
    bool
    isSupported(uint256 const& amendment) const override;

    bool
    hasUnsupportedEnabled() const override;

    std::optional<NetClock::time_point>
    firstUnsupportedExpected() const override;

    Json::Value
    getJson(bool isAdmin) const override;
    Json::Value
    getJson(uint256 const&, bool isAdmin) const override;

    bool
    needValidatedLedger(LedgerIndex seq) const override;

    void
    doValidatedLedger(
        LedgerIndex seq,
        std::set<uint256> const& enabled,
        majorityAmendments_t const& majority) override;

    void
    trustChanged(hash_set<PublicKey> const& allTrusted) override;

    std::vector<uint256>
    doValidation(std::set<uint256> const& enabledAmendments) const override;

    std::vector<uint256>
    getDesired() const override;

    std::map<uint256, std::uint32_t>
    doVoting(
        Rules const& rules,
        NetClock::time_point closeTime,
        std::set<uint256> const& enabledAmendments,
        majorityAmendments_t const& majorityAmendments,
        std::vector<std::shared_ptr<STValidation>> const& validations) override;
};

//------------------------------------------------------------------------------

AmendmentTableImpl::AmendmentTableImpl(
    Application& app,
    std::chrono::seconds majorityTime,
    std::vector<FeatureInfo> const& supported,
    Section const& enabled,
    Section const& vetoed,
    beast::Journal journal)
    : lastUpdateSeq_(0)
    , majorityTime_(majorityTime)
    , unsupportedEnabled_(false)
    , j_(journal)
    , db_(app.getWalletDB())
{
    std::lock_guard lock(mutex_);

    // Find out if the FeatureVotes table exists in WalletDB
    bool const featureVotesExist = [this]() {
        auto db = db_.checkoutDb();
        return createFeatureVotes(*db);
    }();

    // Parse supported amendments
    for (auto const& [name, amendment, votebehavior] : supported)
    {
        AmendmentState& s = add(amendment, lock);

        s.name = name;
        s.supported = true;
        switch (votebehavior)
        {
            case VoteBehavior::DefaultYes:
                s.vote = AmendmentVote::up;
                break;

            case VoteBehavior::DefaultNo:
                s.vote = AmendmentVote::down;
                break;

            case VoteBehavior::Obsolete:
                s.vote = AmendmentVote::obsolete;
                break;
        }

        JLOG(j_.debug()) << "Amendment " << amendment << " (" << s.name
                         << ") is supported and will be "
                         << (s.vote == AmendmentVote::up ? "up" : "down")
                         << " voted by default if not enabled on the ledger.";
    }

    hash_set<uint256> detect_conflict;
    // Parse enabled amendments from config
    for (std::pair<uint256, std::string> const& a : parseSection(enabled))
    {
        if (featureVotesExist)
        {  // If the table existed, warn about duplicate config info
            JLOG(j_.warn()) << "[amendments] section in config file ignored"
                               " in favor of data in db/wallet.db.";
            break;
        }
        else
        {  // Otherwise transfer config data into the table
            detect_conflict.insert(a.first);
            persistVote(a.first, a.second, AmendmentVote::up);
        }
    }

    // Parse vetoed amendments from config
    for (auto const& a : parseSection(vetoed))
    {
        if (featureVotesExist)
        {  // If the table existed, warn about duplicate config info
            JLOG(j_.warn())
                << "[veto_amendments] section in config file ignored"
                   " in favor of data in db/wallet.db.";
            break;
        }
        else
        {  // Otherwise transfer config data into the table
            if (detect_conflict.count(a.first) == 0)
            {
                persistVote(a.first, a.second, AmendmentVote::down);
            }
            else
            {
                JLOG(j_.warn())
                    << "[veto_amendments] section in config has amendment "
                    << '(' << a.first << ", " << a.second
                    << ") both [veto_amendments] and [amendments].";
            }
        }
    }

    // Read amendment votes from wallet.db
    auto db = db_.checkoutDb();
    readAmendments(
        *db,
        [&](boost::optional<std::string> amendment_hash,
            boost::optional<std::string> amendment_name,
            boost::optional<AmendmentVote> vote) {
            uint256 amend_hash;
            if (!amendment_hash || !amendment_name || !vote)
            {
                // These fields should never have nulls, but check
                Throw<std::runtime_error>(
                    "Invalid FeatureVotes row in wallet.db");
            }
            if (!amend_hash.parseHex(*amendment_hash))
            {
                Throw<std::runtime_error>(
                    "Invalid amendment ID '" + *amendment_hash +
                    " in wallet.db");
            }
            if (*vote == AmendmentVote::down)
            {
                // Unknown amendments are effectively vetoed already
                if (auto s = get(amend_hash, lock))
                {
                    JLOG(j_.info()) << "Amendment {" << *amendment_name << ", "
                                    << amend_hash << "} is downvoted.";
                    if (!amendment_name->empty())
                        s->name = *amendment_name;
                    // An obsolete amendment's vote can never be changed
                    if (s->vote != AmendmentVote::obsolete)
                        s->vote = *vote;
                }
            }
            else  // up-vote
            {
                AmendmentState& s = add(amend_hash, lock);

                JLOG(j_.debug()) << "Amendment {" << *amendment_name << ", "
                                 << amend_hash << "} is upvoted.";
                if (!amendment_name->empty())
                    s.name = *amendment_name;
                // An obsolete amendment's vote can never be changed
                if (s.vote != AmendmentVote::obsolete)
                    s.vote = *vote;
            }
        });
}

AmendmentState&
AmendmentTableImpl::add(
    uint256 const& amendmentHash,
    std::lock_guard<std::mutex> const&)
{
    // call with the mutex held
    return amendmentMap_[amendmentHash];
}

AmendmentState*
AmendmentTableImpl::get(
    uint256 const& amendmentHash,
    std::lock_guard<std::mutex> const& lock)
{
    // Forward to the const version of get.
    return const_cast<AmendmentState*>(
        std::as_const(*this).get(amendmentHash, lock));
}

AmendmentState const*
AmendmentTableImpl::get(
    uint256 const& amendmentHash,
    std::lock_guard<std::mutex> const&) const
{
    // call with the mutex held
    auto ret = amendmentMap_.find(amendmentHash);

    if (ret == amendmentMap_.end())
        return nullptr;

    return &ret->second;
}

uint256
AmendmentTableImpl::find(std::string const& name) const
{
    std::lock_guard lock(mutex_);

    for (auto const& e : amendmentMap_)
    {
        if (name == e.second.name)
            return e.first;
    }

    return {};
}

void
AmendmentTableImpl::persistVote(
    uint256 const& amendment,
    std::string const& name,
    AmendmentVote vote) const
{
    ASSERT(
        vote != AmendmentVote::obsolete,
        "ripple::AmendmentTableImpl::persistVote : valid vote input");
    auto db = db_.checkoutDb();
    voteAmendment(*db, amendment, name, vote);
}

bool
AmendmentTableImpl::veto(uint256 const& amendment)
{
    std::lock_guard lock(mutex_);
    AmendmentState& s = add(amendment, lock);

    if (s.vote != AmendmentVote::up)
        return false;
    s.vote = AmendmentVote::down;
    persistVote(amendment, s.name, s.vote);
    return true;
}

bool
AmendmentTableImpl::unVeto(uint256 const& amendment)
{
    std::lock_guard lock(mutex_);
    AmendmentState* const s = get(amendment, lock);

    if (!s || s->vote != AmendmentVote::down)
        return false;
    s->vote = AmendmentVote::up;
    persistVote(amendment, s->name, s->vote);
    return true;
}

bool
AmendmentTableImpl::enable(uint256 const& amendment)
{
    std::lock_guard lock(mutex_);
    AmendmentState& s = add(amendment, lock);

    if (s.enabled)
        return false;

    s.enabled = true;

    if (!s.supported)
    {
        JLOG(j_.error()) << "Unsupported amendment " << amendment
                         << " activated.";
        unsupportedEnabled_ = true;
    }

    return true;
}

bool
AmendmentTableImpl::isEnabled(uint256 const& amendment) const
{
    std::lock_guard lock(mutex_);
    AmendmentState const* s = get(amendment, lock);
    return s && s->enabled;
}

bool
AmendmentTableImpl::isSupported(uint256 const& amendment) const
{
    std::lock_guard lock(mutex_);
    AmendmentState const* s = get(amendment, lock);
    return s && s->supported;
}

bool
AmendmentTableImpl::hasUnsupportedEnabled() const
{
    std::lock_guard lock(mutex_);
    return unsupportedEnabled_;
}

std::optional<NetClock::time_point>
AmendmentTableImpl::firstUnsupportedExpected() const
{
    std::lock_guard lock(mutex_);
    return firstUnsupportedExpected_;
}

std::vector<uint256>
AmendmentTableImpl::doValidation(std::set<uint256> const& enabled) const
{
    // Get the list of amendments we support and do not
    // veto, but that are not already enabled
    std::vector<uint256> amendments;

    {
        std::lock_guard lock(mutex_);
        amendments.reserve(amendmentMap_.size());
        for (auto const& e : amendmentMap_)
        {
            if (e.second.supported && e.second.vote == AmendmentVote::up &&
                (enabled.count(e.first) == 0))
            {
                amendments.push_back(e.first);
                JLOG(j_.info()) << "Voting for amendment " << e.second.name;
            }
        }
    }

    if (!amendments.empty())
        std::sort(amendments.begin(), amendments.end());

    return amendments;
}

std::vector<uint256>
AmendmentTableImpl::getDesired() const
{
    // Get the list of amendments we support and do not veto
    return doValidation({});
}

std::map<uint256, std::uint32_t>
AmendmentTableImpl::doVoting(
    Rules const& rules,
    NetClock::time_point closeTime,
    std::set<uint256> const& enabledAmendments,
    majorityAmendments_t const& majorityAmendments,
    std::vector<std::shared_ptr<STValidation>> const& valSet)
{
    JLOG(j_.trace()) << "voting at " << closeTime.time_since_epoch().count()
                     << ": " << enabledAmendments.size() << ", "
                     << majorityAmendments.size() << ", " << valSet.size();

    std::lock_guard lock(mutex_);

    // Keep a record of the votes we received.
    previousTrustedVotes_.recordVotes(rules, valSet, closeTime, lock);

    // Tally the most recent votes.
    auto vote =
        std::make_unique<AmendmentSet>(rules, previousTrustedVotes_, lock);
    JLOG(j_.debug()) << "Received " << vote->trustedValidations()
                     << " trusted validations, threshold is: "
                     << vote->threshold();

    // Map of amendments to the action to be taken for each one. The action is
    // the value of the flags in the pseudo-transaction
    std::map<uint256, std::uint32_t> actions;

    // process all amendments we know of
    for (auto const& entry : amendmentMap_)
    {
        NetClock::time_point majorityTime = {};

        bool const hasValMajority = vote->passes(entry.first);

        {
            auto const it = majorityAmendments.find(entry.first);
            if (it != majorityAmendments.end())
                majorityTime = it->second;
        }

        if (enabledAmendments.count(entry.first) != 0)
        {
            JLOG(j_.debug()) << entry.first << ": amendment already enabled";
        }
        else if (
            hasValMajority && (majorityTime == NetClock::time_point{}) &&
            entry.second.vote == AmendmentVote::up)
        {
            // Ledger says no majority, validators say yes
            JLOG(j_.debug()) << entry.first << ": amendment got majority";
            actions[entry.first] = tfGotMajority;
        }
        else if (!hasValMajority && (majorityTime != NetClock::time_point{}))
        {
            // Ledger says majority, validators say no
            JLOG(j_.debug()) << entry.first << ": amendment lost majority";
            actions[entry.first] = tfLostMajority;
        }
        else if (
            (majorityTime != NetClock::time_point{}) &&
            ((majorityTime + majorityTime_) <= closeTime) &&
            entry.second.vote == AmendmentVote::up)
        {
            // Ledger says majority held
            JLOG(j_.debug()) << entry.first << ": amendment majority held";
            actions[entry.first] = 0;
        }
    }

    // Stash for reporting
    lastVote_ = std::move(vote);
    return actions;
}

bool
AmendmentTableImpl::needValidatedLedger(LedgerIndex ledgerSeq) const
{
    std::lock_guard lock(mutex_);

    // Is there a ledger in which an amendment could have been enabled
    // between these two ledger sequences?

    return ((ledgerSeq - 1) / 256) != ((lastUpdateSeq_ - 1) / 256);
}

void
AmendmentTableImpl::doValidatedLedger(
    LedgerIndex ledgerSeq,
    std::set<uint256> const& enabled,
    majorityAmendments_t const& majority)
{
    for (auto& e : enabled)
        enable(e);

    std::lock_guard lock(mutex_);

    // Remember the ledger sequence of this update.
    lastUpdateSeq_ = ledgerSeq;

    // Since we have the whole list in `majority`, reset the time flag, even
    // if it's currently set. If it's not set when the loop is done, then any
    // prior unknown amendments have lost majority.
    firstUnsupportedExpected_.reset();
    for (auto const& [hash, time] : majority)
    {
        AmendmentState& s = add(hash, lock);

        if (s.enabled)
            continue;

        if (!s.supported)
        {
            JLOG(j_.info()) << "Unsupported amendment " << hash
                            << " reached majority at " << to_string(time);
            if (!firstUnsupportedExpected_ || firstUnsupportedExpected_ > time)
                firstUnsupportedExpected_ = time;
        }
    }
    if (firstUnsupportedExpected_)
        firstUnsupportedExpected_ = *firstUnsupportedExpected_ + majorityTime_;
}

void
AmendmentTableImpl::trustChanged(hash_set<PublicKey> const& allTrusted)
{
    std::lock_guard lock(mutex_);
    previousTrustedVotes_.trustChanged(allTrusted, lock);
}

void
AmendmentTableImpl::injectJson(
    Json::Value& v,
    const uint256& id,
    const AmendmentState& fs,
    bool isAdmin,
    std::lock_guard<std::mutex> const&) const
{
    if (!fs.name.empty())
        v[jss::name] = fs.name;

    v[jss::supported] = fs.supported;
    if (!fs.enabled && isAdmin)
    {
        if (fs.vote == AmendmentVote::obsolete)
            v[jss::vetoed] = "Obsolete";
        else
            v[jss::vetoed] = fs.vote == AmendmentVote::down;
    }
    v[jss::enabled] = fs.enabled;

    if (!fs.enabled && lastVote_ && isAdmin)
    {
        auto const votesTotal = lastVote_->trustedValidations();
        auto const votesNeeded = lastVote_->threshold();
        auto const votesFor = lastVote_->votes(id);

        v[jss::count] = votesFor;
        v[jss::validations] = votesTotal;

        if (votesNeeded)
            v[jss::threshold] = votesNeeded;
    }
}

Json::Value
AmendmentTableImpl::getJson(bool isAdmin) const
{
    Json::Value ret(Json::objectValue);
    {
        std::lock_guard lock(mutex_);
        for (auto const& e : amendmentMap_)
        {
            injectJson(
                ret[to_string(e.first)] = Json::objectValue,
                e.first,
                e.second,
                isAdmin,
                lock);
        }
    }
    return ret;
}

Json::Value
AmendmentTableImpl::getJson(uint256 const& amendmentID, bool isAdmin) const
{
    Json::Value ret = Json::objectValue;

    {
        std::lock_guard lock(mutex_);
        AmendmentState const* a = get(amendmentID, lock);
        if (a)
        {
            Json::Value& jAmendment =
                (ret[to_string(amendmentID)] = Json::objectValue);
            injectJson(jAmendment, amendmentID, *a, isAdmin, lock);
        }
    }

    return ret;
}

std::unique_ptr<AmendmentTable>
make_AmendmentTable(
    Application& app,
    std::chrono::seconds majorityTime,
    std::vector<AmendmentTable::FeatureInfo> const& supported,
    Section const& enabled,
    Section const& vetoed,
    beast::Journal journal)
{
    return std::make_unique<AmendmentTableImpl>(
        app, majorityTime, supported, enabled, vetoed, journal);
}

}  // namespace ripple
