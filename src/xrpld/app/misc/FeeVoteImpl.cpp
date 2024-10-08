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

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/FeeVote.h>
#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/st.h>

namespace ripple {

namespace detail {

class VotableValue
{
private:
    using value_type = XRPAmount;
    value_type const current_;  // The current setting
    value_type const target_;   // The setting we want
    std::map<value_type, int> voteMap_;

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

    value_type
    current() const
    {
        return current_;
    }

    std::pair<value_type, bool>
    getVotes() const;
};

auto
VotableValue::getVotes() const -> std::pair<value_type, bool>
{
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

    return {ourVote, ourVote != current_};
}

}  // namespace detail

//------------------------------------------------------------------------------

class FeeVoteImpl : public FeeVote
{
private:
    FeeSetup target_;
    beast::Journal const journal_;

public:
    FeeVoteImpl(FeeSetup const& setup, beast::Journal journal);

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

FeeVoteImpl::FeeVoteImpl(FeeSetup const& setup, beast::Journal journal)
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
    if (rules.enabled(featureXRPFees))
    {
        auto vote = [&v, this](
                        auto const current,
                        XRPAmount target,
                        const char* name,
                        auto const& sfield) {
            if (current != target)
            {
                JLOG(journal_.info())
                    << "Voting for " << name << " of " << target;

                v[sfield] = target;
            }
        };
        vote(lastFees.base, target_.reference_fee, "base fee", sfBaseFeeDrops);
        vote(
            lastFees.accountReserve(0),
            target_.account_reserve,
            "base reserve",
            sfReserveBaseDrops);
        vote(
            lastFees.increment,
            target_.owner_reserve,
            "reserve increment",
            sfReserveIncrementDrops);
    }
    else
    {
        auto to32 = [](XRPAmount target) {
            return target.dropsAs<std::uint32_t>();
        };
        auto to64 = [](XRPAmount target) {
            return target.dropsAs<std::uint64_t>();
        };
        auto vote = [&v, this](
                        auto const current,
                        XRPAmount target,
                        auto const& convertCallback,
                        const char* name,
                        auto const& sfield) {
            if (current != target)
            {
                JLOG(journal_.info())
                    << "Voting for " << name << " of " << target;

                if (auto const f = convertCallback(target))
                    v[sfield] = *f;
            }
        };

        vote(lastFees.base, target_.reference_fee, to64, "base fee", sfBaseFee);
        vote(
            lastFees.accountReserve(0),
            target_.account_reserve,
            to32,
            "base reserve",
            sfReserveBase);
        vote(
            lastFees.increment,
            target_.owner_reserve,
            to32,
            "reserve increment",
            sfReserveIncrement);
    }
}

void
FeeVoteImpl::doVoting(
    std::shared_ptr<ReadView const> const& lastClosedLedger,
    std::vector<std::shared_ptr<STValidation>> const& set,
    std::shared_ptr<SHAMap> const& initialPosition)
{
    // LCL must be flag ledger
    ASSERT(
        lastClosedLedger && isFlagLedger(lastClosedLedger->seq()),
        "ripple::FeeVoteImpl::doVoting : has a flag ledger");

    detail::VotableValue baseFeeVote(
        lastClosedLedger->fees().base, target_.reference_fee);

    detail::VotableValue baseReserveVote(
        lastClosedLedger->fees().accountReserve(0), target_.account_reserve);

    detail::VotableValue incReserveVote(
        lastClosedLedger->fees().increment, target_.owner_reserve);

    auto const& rules = lastClosedLedger->rules();
    if (rules.enabled(featureXRPFees))
    {
        auto doVote = [](std::shared_ptr<STValidation> const& val,
                         detail::VotableValue& value,
                         SF_AMOUNT const& xrpField) {
            if (auto const field = ~val->at(~xrpField);
                field && field->native())
            {
                auto const vote = field->xrp();
                if (isLegalAmountSigned(vote))
                    value.addVote(vote);
                else
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
            doVote(val, baseFeeVote, sfBaseFeeDrops);
            doVote(val, baseReserveVote, sfReserveBaseDrops);
            doVote(val, incReserveVote, sfReserveIncrementDrops);
        }
    }
    else
    {
        auto doVote = [](std::shared_ptr<STValidation> const& val,
                         detail::VotableValue& value,
                         auto const& valueField) {
            if (auto const field = val->at(~valueField))
            {
                using xrptype = XRPAmount::value_type;
                auto const vote = *field;
                if (vote <= std::numeric_limits<xrptype>::max() &&
                    isLegalAmountSigned(XRPAmount{unsafe_cast<xrptype>(vote)}))
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
            doVote(val, baseFeeVote, sfBaseFee);
            doVote(val, baseReserveVote, sfReserveBase);
            doVote(val, incReserveVote, sfReserveIncrement);
        }
    }

    // choose our positions
    // TODO: Use structured binding once LLVM 16 is the minimum supported
    // version. See also: https://github.com/llvm/llvm-project/issues/48582
    // https://github.com/llvm/llvm-project/commit/127bf44385424891eb04cff8e52d3f157fc2cb7c
    auto const baseFee = baseFeeVote.getVotes();
    auto const baseReserve = baseReserveVote.getVotes();
    auto const incReserve = incReserveVote.getVotes();

    auto const seq = lastClosedLedger->info().seq + 1;

    // add transactions to our position
    if (baseFee.second || baseReserve.second || incReserve.second)
    {
        JLOG(journal_.warn())
            << "We are voting for a fee change: " << baseFee.first << "/"
            << baseReserve.first << "/" << incReserve.first;

        STTx feeTx(ttFEE, [=, &rules](auto& obj) {
            obj[sfAccount] = AccountID();
            obj[sfLedgerSequence] = seq;
            if (rules.enabled(featureXRPFees))
            {
                obj[sfBaseFeeDrops] = baseFee.first;
                obj[sfReserveBaseDrops] = baseReserve.first;
                obj[sfReserveIncrementDrops] = incReserve.first;
            }
            else
            {
                // Without the featureXRPFees amendment, these fields are
                // required.
                obj[sfBaseFee] =
                    baseFee.first.dropsAs<std::uint64_t>(baseFeeVote.current());
                obj[sfReserveBase] = baseReserve.first.dropsAs<std::uint32_t>(
                    baseReserveVote.current());
                obj[sfReserveIncrement] =
                    incReserve.first.dropsAs<std::uint32_t>(
                        incReserveVote.current());
                obj[sfReferenceFeeUnits] = Config::FEE_UNITS_DEPRECATED;
            }
        });

        uint256 txID = feeTx.getTransactionID();

        JLOG(journal_.warn()) << "Vote: " << txID;

        Serializer s;
        feeTx.add(s);

        if (!initialPosition->addGiveItem(
                SHAMapNodeType::tnTRANSACTION_NM,
                make_shamapitem(txID, s.slice())))
        {
            JLOG(journal_.warn()) << "Ledger already had fee change";
        }
    }
}

//------------------------------------------------------------------------------

std::unique_ptr<FeeVote>
make_FeeVote(FeeSetup const& setup, beast::Journal journal)
{
    return std::make_unique<FeeVoteImpl>(setup, journal);
}

}  // namespace ripple
