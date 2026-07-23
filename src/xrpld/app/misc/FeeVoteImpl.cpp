#include <xrpld/app/misc/FeeVote.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Protocol.h>  // IWYU pragma: keep
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl {

namespace detail {

template <typename ValueType>
class VotableValue
{
private:
    ValueType const current_;  // The current setting
    ValueType const target_;   // The setting we want
    std::map<ValueType, int> voteMap_;

public:
    VotableValue(ValueType current, ValueType target) : current_(current), target_(target)
    {
        // Add our vote
        ++voteMap_[target_];
    }

    void
    addVote(ValueType vote)
    {
        ++voteMap_[vote];
    }

    void
    noVote()
    {
        addVote(current_);
    }

    [[nodiscard]] ValueType
    current() const
    {
        return current_;
    }

    [[nodiscard]] std::pair<ValueType, bool>
    getVotes() const;
};

template <typename ValueType>
std::pair<ValueType, bool>
VotableValue<ValueType>::getVotes() const
{
    ValueType ourVote = current_;
    int weight = 0;
    for (auto const& [key, val] : voteMap_)
    {
        // Take most voted value between current and target, inclusive
        if ((key <= std::max(target_, current_)) && (key >= std::min(target_, current_)) &&
            (val > weight))
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
    doValidation(Fees const& lastFees, Rules const& rules, STValidation& val) override;

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
FeeVoteImpl::doValidation(Fees const& lastFees, Rules const& rules, STValidation& v)
{
    // Values should always be in a valid range (because the voting process
    // will ignore out-of-range values) but if we detect such a case, we do
    // not send a value.
    auto vote = [&v, this](auto const current, auto target, char const* name, auto const& sfield) {
        if (current != target)
        {
            JLOG(journal_.info()) << "Voting for " << name << " of " << target;

            v[sfield] = target;
        }
    };
    if (rules.enabled(featureXRPFees))
    {
        vote(lastFees.base, target_.referenceFee, "base fee", sfBaseFeeDrops);
        vote(lastFees.reserve, target_.accountReserve, "base reserve", sfReserveBaseDrops);
        vote(
            lastFees.increment, target_.ownerReserve, "reserve increment", sfReserveIncrementDrops);
    }
    else
    {
        auto to32 = [](XRPAmount target) { return target.dropsAs<std::uint32_t>(); };
        auto to64 = [](XRPAmount target) { return target.dropsAs<std::uint64_t>(); };
        auto voteAndConvert = [&v, this](
                                  auto const current,
                                  XRPAmount target,
                                  auto const& convertCallback,
                                  char const* name,
                                  auto const& sfield) {
            if (current != target)
            {
                JLOG(journal_.info()) << "Voting for " << name << " of " << target;

                if (auto const f = convertCallback(target))
                    v[sfield] = *f;
            }
        };

        voteAndConvert(lastFees.base, target_.referenceFee, to64, "base fee", sfBaseFee);
        voteAndConvert(
            lastFees.reserve, target_.accountReserve, to32, "base reserve", sfReserveBase);
        voteAndConvert(
            lastFees.increment,
            target_.ownerReserve,
            to32,
            "reserve increment",
            sfReserveIncrement);
    }
    if (rules.enabled(featureSmartEscrow))
    {
        if (target_.gas_limit <= kMaxGasLimit)
        {
            vote(lastFees.gasLimit, target_.gas_limit, "gas limit", sfGasLimit);
        }
        if (target_.bytecode_size_limit <= kMaxBytecodeSizeLimit)
        {
            vote(
                lastFees.bytecodeSizeLimit,
                target_.bytecode_size_limit,
                "bytecode size limit",
                sfBytecodeSizeLimit);
        }
        vote(lastFees.gasPrice, target_.gas_price, "gas price", sfGasPrice);
    }
}

void
FeeVoteImpl::doVoting(
    std::shared_ptr<ReadView const> const& lastClosedLedger,
    std::vector<std::shared_ptr<STValidation>> const& set,
    std::shared_ptr<SHAMap> const& initialPosition)
{
    // LCL must be flag ledger
    XRPL_ASSERT(
        lastClosedLedger && isFlagLedger(lastClosedLedger->seq()),
        "xrpl::FeeVoteImpl::doVoting : has a flag ledger");

    detail::VotableValue baseFeeVote(lastClosedLedger->fees().base, target_.referenceFee);

    detail::VotableValue baseReserveVote(lastClosedLedger->fees().reserve, target_.accountReserve);

    detail::VotableValue incReserveVote(lastClosedLedger->fees().increment, target_.ownerReserve);

    auto validOrCurrent = [](std::uint32_t target, std::uint32_t max, std::uint32_t current) {
        return target <= max ? target : current;
    };

    detail::VotableValue gasLimitVote(
        lastClosedLedger->fees().gasLimit,
        validOrCurrent(target_.gas_limit, kMaxGasLimit, lastClosedLedger->fees().gasLimit));

    detail::VotableValue bytecodeSizeLimitVote(
        lastClosedLedger->fees().bytecodeSizeLimit,
        validOrCurrent(
            target_.bytecode_size_limit,
            kMaxBytecodeSizeLimit,
            lastClosedLedger->fees().bytecodeSizeLimit));

    detail::VotableValue gasPriceVote(lastClosedLedger->fees().gasPrice, target_.gas_price);

    auto const& rules = lastClosedLedger->rules();
    if (rules.enabled(featureXRPFees))
    {
        auto doVote = [](std::shared_ptr<STValidation> const& val,
                         detail::VotableValue<XRPAmount>& value,
                         SF_AMOUNT const& xrpField) {
            if (auto const field = ~val->at(~xrpField); field && field->native())
            {
                auto const vote = field->xrp();
                if (isLegalAmountSigned(vote))
                {
                    value.addVote(vote);
                }
                else
                {
                    value.noVote();
                }
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
                         detail::VotableValue<XRPAmount>& value,
                         auto const& valueField) {
            if (auto const field = val->at(~valueField))
            {
                using XRPType = XRPAmount::value_type;
                auto const vote = *field;
                if (vote <= std::numeric_limits<XRPType>::max() &&
                    isLegalAmountSigned(XRPAmount{unsafeCast<XRPType>(vote)}))
                {
                    value.addVote(XRPAmount{unsafeCast<XRPType>(vote)});
                }
                else
                {
                    // Invalid amounts will be treated as if they're
                    // not provided. Don't throw because this value is
                    // provided by an external entity.
                    value.noVote();
                }
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
    if (rules.enabled(featureSmartEscrow))
    {
        auto doVote = [](std::shared_ptr<STValidation> const& val,
                         detail::VotableValue<std::uint32_t>& value,
                         SF_UINT32 const& sfield,
                         std::uint32_t maxValue) {
            if (auto const field = ~val->at(~sfield); field)
            {
                if (field.value() <= maxValue)
                {
                    value.addVote(field.value());
                }
                else
                {
                    value.noVote();
                }
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
            doVote(val, gasLimitVote, sfGasLimit, kMaxGasLimit);
            doVote(val, bytecodeSizeLimitVote, sfBytecodeSizeLimit, kMaxBytecodeSizeLimit);
            doVote(val, gasPriceVote, sfGasPrice, std::numeric_limits<std::uint32_t>::max());
        }
    }

    // choose our positions
    // TODO: Use structured binding once LLVM 16 is the minimum supported
    // version. See also: https://github.com/llvm/llvm-project/issues/48582
    // https://github.com/llvm/llvm-project/commit/127bf44385424891eb04cff8e52d3f157fc2cb7c
    auto const baseFee = baseFeeVote.getVotes();
    auto const baseReserve = baseReserveVote.getVotes();
    auto const incReserve = incReserveVote.getVotes();
    auto const gasLimit = gasLimitVote.getVotes();
    auto const bytecodeSizeLimit = bytecodeSizeLimitVote.getVotes();
    auto const gasPrice = gasPriceVote.getVotes();

    auto const seq = lastClosedLedger->header().seq + 1;

    // add transactions to our position
    if (baseFee.second || baseReserve.second || incReserve.second || gasLimit.second ||
        bytecodeSizeLimit.second || gasPrice.second)
    {
        JLOG(journal_.warn()) << "We are voting for a fee change: " << baseFee.first << "/"
                              << baseReserve.first << "/" << incReserve.first;

        STTx const feeTx(ttFEE, [=, &rules](auto& obj) {
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
                obj[sfBaseFee] = baseFee.first.dropsAs<std::uint64_t>(baseFeeVote.current());
                obj[sfReserveBase] =
                    baseReserve.first.dropsAs<std::uint32_t>(baseReserveVote.current());
                obj[sfReserveIncrement] =
                    incReserve.first.dropsAs<std::uint32_t>(incReserveVote.current());
                obj[sfReferenceFeeUnits] = kFeeUnitsDeprecated;
            }
            if (rules.enabled(featureSmartEscrow))
            {
                obj[sfGasLimit] = gasLimit.first;
                obj[sfBytecodeSizeLimit] = bytecodeSizeLimit.first;
                obj[sfGasPrice] = gasPrice.first;
            }
        });

        uint256 const txID = feeTx.getTransactionID();

        JLOG(journal_.warn()) << "Vote: " << txID;

        Serializer s;
        feeTx.add(s);

        if (!initialPosition->addGiveItem(
                SHAMapNodeType::TnTransactionNm, makeShamapitem(txID, s.slice())))
        {
            JLOG(journal_.warn()) << "Ledger already had fee change";
        }
    }
}

//------------------------------------------------------------------------------

std::unique_ptr<FeeVote>
makeFeeVote(FeeSetup const& setup, beast::Journal journal)
{
    return std::make_unique<FeeVoteImpl>(setup, journal);
}

}  // namespace xrpl
