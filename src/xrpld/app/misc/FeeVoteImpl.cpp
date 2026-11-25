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
    using value_type = ValueType;
    value_type const current_;  // The current setting
    value_type const target_;   // The setting we want
    std::map<value_type, int> voteMap_;

public:
    VotableValue(value_type current, value_type target) : current_(current), target_(target)
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

    [[nodiscard]] value_type
    current() const
    {
        return current_;
    }

    [[nodiscard]] std::pair<value_type, bool>
    getVotes() const;
};

template <typename ValueType>
auto
VotableValue<ValueType>::getVotes() const -> std::pair<value_type, bool>
{
    value_type ourVote = current_;
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
    auto const vote = [&v, this](
                          auto const current, auto target, char const* name, auto const& sfield) {
        if (current != target)
        {
            JLOG(journal_.info()) << "Voting for " << name << " of " << target;

            v[sfield] = target;
        }
    };
    vote(lastFees.base, target_.referenceFee, "base fee", sfBaseFeeDrops);
    vote(lastFees.reserve, target_.accountReserve, "base reserve", sfReserveBaseDrops);
    vote(lastFees.increment, target_.ownerReserve, "reserve increment", sfReserveIncrementDrops);
    if (rules.enabled(featureSmartEscrow))
    {
        if (target_.gasLimit <= kMaxGasLimit)
        {
            vote(lastFees.gasLimit, target_.gasLimit, "gas limit", sfGasLimit);
        }
        if (target_.bytecodeSizeLimit <= kMaxBytecodeSizeLimit)
        {
            vote(
                lastFees.bytecodeSizeLimit,
                target_.bytecodeSizeLimit,
                "bytecode size limit",
                sfBytecodeSizeLimit);
        }
        if (target_.gasPrice >= kMinGasPrice)
        {
            vote(lastFees.gasPrice, target_.gasPrice, "gas price", sfGasPrice);
        }
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

    auto const validOrCurrent =
        [](std::uint32_t target, std::uint32_t min, std::uint32_t max, std::uint32_t current) {
            return (target >= min && target <= max) ? target : current;
        };

    detail::VotableValue gasLimitVote(
        lastClosedLedger->fees().gasLimit,
        validOrCurrent(target_.gasLimit, 0, kMaxGasLimit, lastClosedLedger->fees().gasLimit));

    detail::VotableValue bytecodeSizeLimitVote(
        lastClosedLedger->fees().bytecodeSizeLimit,
        validOrCurrent(
            target_.bytecodeSizeLimit,
            0,
            kMaxBytecodeSizeLimit,
            lastClosedLedger->fees().bytecodeSizeLimit));

    detail::VotableValue gasPriceVote(
        lastClosedLedger->fees().gasPrice,
        validOrCurrent(
            target_.gasPrice,
            kMinGasPrice,
            std::numeric_limits<std::uint32_t>::max(),
            lastClosedLedger->fees().gasPrice));

    auto const doVote = [](std::shared_ptr<STValidation> const& val,
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

    auto const& rules = lastClosedLedger->rules();
    if (rules.enabled(featureSmartEscrow))
    {
        auto const doExtensionVote = [](std::shared_ptr<STValidation> const& val,
                                        detail::VotableValue<std::uint32_t>& value,
                                        SF_UINT32 const& sfield,
                                        std::uint32_t minValue,
                                        std::uint32_t maxValue) {
            if (auto const field = ~val->at(~sfield);
                field && *field >= minValue && *field <= maxValue)
            {
                value.addVote(*field);
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
            doExtensionVote(val, gasLimitVote, sfGasLimit, 0, kMaxGasLimit);
            doExtensionVote(
                val, bytecodeSizeLimitVote, sfBytecodeSizeLimit, 0, kMaxBytecodeSizeLimit);
            doExtensionVote(
                val,
                gasPriceVote,
                sfGasPrice,
                kMinGasPrice,
                std::numeric_limits<std::uint32_t>::max());
        }
    }

    // choose our positions
    auto const [baseFee, baseFeeChanged] = baseFeeVote.getVotes();
    auto const [baseReserve, baseReserveChanged] = baseReserveVote.getVotes();
    auto const [incReserve, incReserveChanged] = incReserveVote.getVotes();
    auto const [gasLimit, gasLimitChanged] = gasLimitVote.getVotes();
    auto const [bytecodeSizeLimit, bytecodeSizeLimitChanged] = bytecodeSizeLimitVote.getVotes();
    auto const [gasPrice, gasPriceChanged] = gasPriceVote.getVotes();

    auto const seq = lastClosedLedger->header().seq + 1;

    // The gas votes only count once the amendment is on. Before it is, the
    // ledger's gas settings may differ from the config targets, and counting
    // that as a change would emit a SetFee on every flag ledger that carries
    // no gas fields and changes nothing.
    bool const smartEscrowFeeChanged = rules.enabled(featureSmartEscrow) &&
        (gasLimitChanged || bytecodeSizeLimitChanged || gasPriceChanged);

    // add transactions to our position
    if (baseFeeChanged || baseReserveChanged || incReserveChanged || smartEscrowFeeChanged)
    {
        JLOG(journal_.warn()) << "We are voting for a fee change: " << baseFee << "/" << baseReserve
                              << "/" << incReserve;

        STTx const feeTx(ttFEE, [=, &rules](auto& obj) {
            obj[sfAccount] = AccountID();
            obj[sfLedgerSequence] = seq;
            obj[sfBaseFeeDrops] = baseFee;
            obj[sfReserveBaseDrops] = baseReserve;
            obj[sfReserveIncrementDrops] = incReserve;
            if (rules.enabled(featureSmartEscrow))
            {
                obj[sfGasLimit] = gasLimit;
                obj[sfBytecodeSizeLimit] = bytecodeSizeLimit;
                obj[sfGasPrice] = gasPrice;
            }
        });

        UInt256 const txID = feeTx.getTransactionID();

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
