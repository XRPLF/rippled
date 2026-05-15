#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <stdexcept>
#include <utility>

namespace xrpl {

template <StepAmount TIn, StepAmount TOut>
class TOffer
{
private:
    SLE::pointer entry_;
    Quality quality_{};
    AccountID account_;
    Asset assetIn_;
    Asset assetOut_;

    TAmounts<TIn, TOut> amounts_{};
    void
    setFieldAmounts();

public:
    TOffer() = default;

    TOffer(SLE::pointer entry, Quality quality);

    /** Returns the quality of the offer.
        Conceptually, the quality is the ratio of output to input currency.
        The implementation calculates it as the ratio of input to output
        currency (so it sorts ascending). The quality is computed at the time
        the offer is placed, and never changes for the lifetime of the offer.
        This is an important business rule that maintains accuracy when an
        offer is partially filled; Subsequent partial fills will use the
        original quality.
    */
    [[nodiscard]] Quality
    quality() const noexcept
    {
        return quality_;
    }

    /** Returns the account id of the offer's owner. */
    [[nodiscard]] AccountID const&
    owner() const
    {
        return account_;
    }

    /** Returns the in and out amounts.
        Some or all of the out amount may be unfunded.
    */
    [[nodiscard]] TAmounts<TIn, TOut> const&
    amount() const
    {
        return amounts_;
    }

    /** Returns `true` if no more funds can flow through this offer. */
    [[nodiscard]] bool
    fullyConsumed() const
    {
        if (amounts_.in <= beast::kZero)
            return true;
        if (amounts_.out <= beast::kZero)
            return true;
        return false;
    }

    /** Adjusts the offer to indicate that we consumed some (or all) of it. */
    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed)
    {
        if (consumed.in > amounts_.in)
            Throw<std::logic_error>("can't consume more than is available.");

        if (consumed.out > amounts_.out)
            Throw<std::logic_error>("can't produce more than is available.");

        amounts_ -= consumed;
        setFieldAmounts();
        view.update(entry_);
    }

    [[nodiscard]] std::string
    id() const
    {
        return to_string(entry_->key());
    }

    [[nodiscard]] std::optional<uint256>
    key() const
    {
        return entry_->key();
    }

    [[nodiscard]] Asset const&
    assetIn() const;
    [[nodiscard]] Asset const&
    assetOut() const;

    [[nodiscard]] TAmounts<TIn, TOut>
    limitOut(TAmounts<TIn, TOut> const& offerAmount, TOut const& limit, bool roundUp) const;

    [[nodiscard]] TAmounts<TIn, TOut>
    limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp) const;

    template <typename... Args>
    static TER
    send(Args&&... args);

    [[nodiscard]] bool
    isFunded() const
    {
        // Offer owner is issuer; they have unlimited funds if IOU
        return account_ == assetOut_.getIssuer() && assetOut_.holds<Issue>();
    }

    static std::pair<std::uint32_t, std::uint32_t>
    adjustRates(std::uint32_t ofrInRate, std::uint32_t ofrOutRate)
    {
        // CLOB offer pays the transfer fee
        return {ofrInRate, ofrOutRate};
    }

    /** Check any required invariant. Limit order book offer
     * always returns true.
     */
    [[nodiscard]] bool
    checkInvariant(TAmounts<TIn, TOut> const& consumed, beast::Journal j) const
    {
        if (!isFeatureEnabled(fixAMMv1_3))
            return true;

        if (consumed.in > amounts_.in || consumed.out > amounts_.out)
        {
            // LCOV_EXCL_START
            JLOG(j.error()) << "AMMOffer::checkInvariant failed: consumed "
                            << to_string(consumed.in) << " " << to_string(consumed.out)
                            << " amounts " << to_string(amounts_.in) << " "
                            << to_string(amounts_.out);

            return false;
            // LCOV_EXCL_STOP
        }

        return true;
    }
};

template <StepAmount TIn, StepAmount TOut>
TOffer<TIn, TOut>::TOffer(SLE::pointer entry, Quality quality)
    : entry_(std::move(entry)), quality_(quality), account_(entry_->getAccountID(sfAccount))
{
    auto const tp = entry_->getFieldAmount(sfTakerPays);
    auto const tg = entry_->getFieldAmount(sfTakerGets);
    amounts_.in = toAmount<TIn>(tp);
    amounts_.out = toAmount<TOut>(tg);
    assetIn_ = tp.asset();
    assetOut_ = tg.asset();
}

template <StepAmount TIn, StepAmount TOut>
void
TOffer<TIn, TOut>::setFieldAmounts()
{
    if constexpr (std::is_same_v<TIn, XRPAmount>)
    {
        entry_->setFieldAmount(sfTakerPays, toSTAmount(amounts_.in));
    }
    else
    {
        entry_->setFieldAmount(sfTakerPays, toSTAmount(amounts_.in, assetIn_));
    }

    if constexpr (std::is_same_v<TOut, XRPAmount>)
    {
        entry_->setFieldAmount(sfTakerGets, toSTAmount(amounts_.out));
    }
    else
    {
        entry_->setFieldAmount(sfTakerGets, toSTAmount(amounts_.out, assetOut_));
    }
}

template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut>
TOffer<TIn, TOut>::limitOut(TAmounts<TIn, TOut> const& offerAmount, TOut const& limit, bool roundUp)
    const
{
    // It turns out that the ceil_out implementation has some slop in
    // it, which ceil_out_strict removes.
    return quality().ceilOutStrict(offerAmount, limit, roundUp);
}

template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut>
TOffer<TIn, TOut>::limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp)
    const
{
    if (auto const& rules = getCurrentTransactionRules();
        rules && rules->enabled(fixReducedOffersV2))
    {
        // It turns out that the ceil_in implementation has some slop in
        // it.  ceil_in_strict removes that slop.  But removing that slop
        // affects transaction outcomes, so the change must be made using
        // an amendment.
        return quality().ceilInStrict(offerAmount, limit, roundUp);
    }
    return quality_.ceilIn(offerAmount, limit);
}

template <StepAmount TIn, StepAmount TOut>
template <typename... Args>
TER
TOffer<TIn, TOut>::send(Args&&... args)
{
    return accountSend(std::forward<Args>(args)..., WaiveTransferFee::No, AllowMPTOverflow::Yes);
}

template <StepAmount TIn, StepAmount TOut>
Asset const&
TOffer<TIn, TOut>::assetIn() const
{
    return assetIn_;
}

template <StepAmount TIn, StepAmount TOut>
Asset const&
TOffer<TIn, TOut>::assetOut() const
{
    return assetOut_;
}

template <StepAmount TIn, StepAmount TOut>
inline std::ostream&
operator<<(std::ostream& os, TOffer<TIn, TOut> const& offer)
{
    return os << offer.id();
}

}  // namespace xrpl
