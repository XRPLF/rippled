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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/st.h>

namespace ripple {

namespace detail {

class VotableValue
{
private:
    using value_type = XRPAmount;
    value_type const current_;  // The current setting
    value_type const target_;   // The setting we want
    std::map<value_type, int> voteMap_;
    std::optional<value_type> vote_;

public:
    VotableValue(value_type current, value_type target)
        : current_(current), target_(target)
    {
        // Add our vote
        ++voteMap_[target_];
    }

    void
    addVote(value_type vote)
    {
        ++voteMap_[vote];
    }

    void
    noVote()
    {
        addVote(current_);
    }

    void
    setVotes();

    value_type
    getVotes() const;

    template <class Dest>
    Dest
    getVotesAs() const;

    bool
    voteChange() const
    {
        return getVotes() != current_;
    }
};

void
VotableValue::setVotes()
{
    // Need to explicitly remove any value from vote_, because it will be
    // returned from getVotes() if set.
    vote_.reset();
    vote_ = getVotes();
}

auto
VotableValue::getVotes() const -> value_type
{
    if (vote_)
        return *vote_;

    value_type ourVote = current_;
    int weight = 0;
    for (auto const& [key, val] : voteMap_)
    {
        // Take most voted value between current and target, inclusive
        if ((key <= std::max(target_, current_)) &&
            (key >= std::min(target_, current_)) && (val > weight))
        {
            ourVote = key;
            weight = val;
        }
    }

    return ourVote;
}

template <class Dest>
Dest
VotableValue::getVotesAs() const
{
    return getVotes().dropsAs<Dest>(current_);
}

}  // namespace detail

//------------------------------------------------------------------------------

class FeeVoteImpl : public FeeVote
{
private:
    Setup target_;
    beast::Journal const journal_;

public:
    FeeVoteImpl(Setup const& setup, beast::Journal journal);

    void
    doValidation(Fees const& lastFees, Rules const& rules, STValidation& val)
        override;

    void
    doVoting(
        std::shared_ptr<ReadView const> const& lastClosedLedger,
        std::vector<std::shared_ptr<STValidation>> const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition) override;
};

//--------------------------------------------------------------------------

FeeVoteImpl::FeeVoteImpl(Setup const& setup, beast::Journal journal)
    : target_(setup), journal_(journal)
{
}

void
FeeVoteImpl::doValidation(
    Fees const& lastFees,
    Rules const& rules,
    STValidation& v)
{
    // Values should always be in a valid range (because the voting process
    // will ignore out-of-range values) but if we detect such a case, we do
    // not send a value.
    if (lastFees.base != target_.reference_fee)
    {
        JLOG(journal_.info())
            << "Voting for base fee of " << target_.reference_fee;

        if (rules.enabled(featureXRPFees))
            v[sfBaseFeeXRP] = target_.reference_fee;
        else if (auto const f = target_.reference_fee.dropsAs<std::uint64_t>())
            v[sfBaseFee] = *f;
    }

    if (lastFees.accountReserve(0) != target_.account_reserve)
    {
        JLOG(journal_.info())
            << "Voting for base reserve of " << target_.account_reserve;

        if (rules.enabled(featureXRPFees))
            v[sfReserveBaseXRP] = target_.account_reserve;
        else if (
            auto const f = target_.account_reserve.dropsAs<std::uint32_t>())
            v[sfReserveBase] = *f;
    }

    if (lastFees.increment != target_.owner_reserve)
    {
        JLOG(journal_.info())
            << "Voting for reserve increment of " << target_.owner_reserve;

        if (rules.enabled(featureXRPFees))
            v[sfReserveIncrementXRP] = target_.owner_reserve;
        else if (auto const f = target_.owner_reserve.dropsAs<std::uint32_t>())
            v[sfReserveIncrement] = *f;
    }
}

void
FeeVoteImpl::doVoting(
    std::shared_ptr<ReadView const> const& lastClosedLedger,
    std::vector<std::shared_ptr<STValidation>> const& set,
    std::shared_ptr<SHAMap> const& initialPosition)
{
    // LCL must be flag ledger
    assert(lastClosedLedger && isFlagLedger(lastClosedLedger->seq()));

    detail::VotableValue baseFeeVote(
        lastClosedLedger->fees().base, target_.reference_fee);

    detail::VotableValue baseReserveVote(
        lastClosedLedger->fees().accountReserve(0), target_.account_reserve);

    detail::VotableValue incReserveVote(
        lastClosedLedger->fees().increment, target_.owner_reserve);

    auto const& rules = lastClosedLedger->rules();
    auto doVote = [&rules](
                      std::shared_ptr<STValidation> const& val,
                      detail::VotableValue& value,
                      SF_AMOUNT const& xrpField,
                      auto const& valueField) {
        if (auto const field = ~val->at(~xrpField);
            field && rules.enabled(featureXRPFees) && field->native())
        {
            auto const vote = field->xrp();
            if (isLegalAmount(vote))
                value.addVote(vote);
            else
                value.noVote();
        }
        else if (auto const field = val->at(~valueField))
        {
            using xrptype = XRPAmount::value_type;
            auto const vote = *field;
            if (vote <= std::numeric_limits<xrptype>::max() &&
                isLegalAmount(XRPAmount{unsafe_cast<xrptype>(vote)}))
                value.addVote(
                    XRPAmount{unsafe_cast<XRPAmount::value_type>(vote)});
            else
                // Invalid amounts will be treated as if they're
                // not provided. Don't throw because this value is
                // provided by an external entity.
                value.noVote();
        }
        else
        {
            value.noVote();
        }
    };

    for (auto const& val : set)
    {
        if (!val->isTrusted())
            continue;
        doVote(val, baseFeeVote, sfBaseFeeXRP, sfBaseFee);
        doVote(val, baseReserveVote, sfReserveBaseXRP, sfReserveBase);
        doVote(val, incReserveVote, sfReserveIncrementXRP, sfReserveIncrement);
    }

    // choose our positions
    baseFeeVote.setVotes();
    baseReserveVote.setVotes();
    incReserveVote.setVotes();

    auto const seq = lastClosedLedger->info().seq + 1;

    // add transactions to our position
    if ((baseFeeVote.voteChange()) || (baseReserveVote.voteChange()) ||
        (incReserveVote.voteChange()))
    {
        JLOG(journal_.warn())
            << "We are voting for a fee change: " << baseFeeVote.getVotes()
            << "/" << baseReserveVote.getVotes() << "/"
            << incReserveVote.getVotes();

        STTx feeTx(
            ttFEE,
            [&rules, seq, &baseFeeVote, &baseReserveVote, &incReserveVote](
                auto& obj) {
                obj[sfAccount] = AccountID();
                obj[sfLedgerSequence] = seq;
                if (rules.enabled(featureXRPFees))
                {
                    obj[sfBaseFeeXRP] = baseFeeVote.getVotes();
                    obj[sfReserveBaseXRP] = baseReserveVote.getVotes();
                    obj[sfReserveIncrementXRP] = incReserveVote.getVotes();
                }
                else
                {
                    // Without the featureXRPFees amendment, these fields are
                    // required.
                    obj[sfBaseFee] = baseFeeVote.getVotesAs<std::uint64_t>();
                    obj[sfReserveBase] =
                        baseReserveVote.getVotesAs<std::uint32_t>();
                    obj[sfReserveIncrement] =
                        incReserveVote.getVotesAs<std::uint32_t>();
                    obj[sfReferenceFeeUnits] = Config::FEE_UNITS_DEPRECATED;
                }
            });

        uint256 txID = feeTx.getTransactionID();

        JLOG(journal_.warn()) << "Vote: " << txID;

        Serializer s;
        feeTx.add(s);

        if (!initialPosition->addGiveItem(
                SHAMapNodeType::tnTRANSACTION_NM,
                std::make_shared<SHAMapItem>(txID, s.slice())))
        {
            JLOG(journal_.warn()) << "Ledger already had fee change";
        }
    }
}

//------------------------------------------------------------------------------

FeeVote::Setup
setup_FeeVote(Section const& section)
{
    FeeVote::Setup setup;
    {
        std::uint64_t temp;
        if (set(temp, "reference_fee", section) &&
            temp <= std::numeric_limits<XRPAmount::value_type>::max())
            setup.reference_fee = temp;
    }
    {
        std::uint32_t temp;
        if (set(temp, "account_reserve", section))
            setup.account_reserve = temp;
        if (set(temp, "owner_reserve", section))
            setup.owner_reserve = temp;
    }
    return setup;
}

std::unique_ptr<FeeVote>
make_FeeVote(FeeVote::Setup const& setup, beast::Journal journal)
{
    return std::make_unique<FeeVoteImpl>(setup, journal);
}

}  // namespace ripple
