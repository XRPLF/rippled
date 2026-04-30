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
    SLE::pointer m_entry;
    Quality m_quality{};
    AccountID m_account;
    Asset assetIn_;
    Asset assetOut_;

    TAmounts<TIn, TOut> m_amounts{};
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
        return m_quality;
    }

    /** Returns the account id of the offer's owner. */
    [[nodiscard]] AccountID const&
    owner() const
    {
        return m_account;
    }

    /** Returns the in and out amounts.
        Some or all of the out amount may be unfunded.
    */
    [[nodiscard]] TAmounts<TIn, TOut> const&
    amount() const
    {
        return m_amounts;
    }

    /** Returns `true` if no more funds can flow through this offer. */
    [[nodiscard]] bool
    fully_consumed() const
    {
        if (m_amounts.in <= beast::zero)
            return true;
        if (m_amounts.out <= beast::zero)
            return true;
        return false;
    }

    /** Adjusts the offer to indicate that we consumed some (or all) of it. */
    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed)
    {
        if (consumed.in > m_amounts.in)
            Throw<std::logic_error>("can't consume more than is available.");

        if (consumed.out > m_amounts.out)
            Throw<std::logic_error>("can't produce more than is available.");

        m_amounts -= consumed;
        setFieldAmounts();
        view.update(m_entry);
    }

    [[nodiscard]] std::string
    id() const
    {
        return to_string(m_entry->key());
    }

    [[nodiscard]] std::optional<uint256>
    key() const
    {
        return m_entry->key();
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
        return m_account == assetOut_.getIssuer() && assetOut_.holds<Issue>();
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

        if (consumed.in > m_amounts.in || consumed.out > m_amounts.out)
        {
            // LCOV_EXCL_START
            JLOG(j.error()) << "AMMOffer::checkInvariant failed: consumed "
                            << to_string(consumed.in) << " " << to_string(consumed.out)
                            << " amounts " << to_string(m_amounts.in) << " "
                            << to_string(m_amounts.out);

            return false;
            // LCOV_EXCL_STOP
        }

        return true;
    }
};

template <StepAmount TIn, StepAmount TOut>
TOffer<TIn, TOut>::TOffer(SLE::pointer entry, Quality quality)
    : m_entry(std::move(entry)), m_quality(quality), m_account(m_entry->getAccountID(sfAccount))
{
    auto const tp = m_entry->getFieldAmount(sfTakerPays);
    auto const tg = m_entry->getFieldAmount(sfTakerGets);
    m_amounts.in = toAmount<TIn>(tp);
    m_amounts.out = toAmount<TOut>(tg);
    assetIn_ = tp.asset();
    assetOut_ = tg.asset();
}

template <StepAmount TIn, StepAmount TOut>
void
TOffer<TIn, TOut>::setFieldAmounts()
{
    if constexpr (std::is_same_v<TIn, XRPAmount>)
    {
        m_entry->setFieldAmount(sfTakerPays, toSTAmount(m_amounts.in));
    }
    else
    {
        m_entry->setFieldAmount(sfTakerPays, toSTAmount(m_amounts.in, assetIn_));
    }

    if constexpr (std::is_same_v<TOut, XRPAmount>)
    {
        m_entry->setFieldAmount(sfTakerGets, toSTAmount(m_amounts.out));
    }
    else
    {
        m_entry->setFieldAmount(sfTakerGets, toSTAmount(m_amounts.out, assetOut_));
    }
}

template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut>
TOffer<TIn, TOut>::limitOut(TAmounts<TIn, TOut> const& offerAmount, TOut const& limit, bool roundUp)
    const
{
    // It turns out that the ceil_out implementation has some slop in
    // it, which ceil_out_strict removes.
    return quality().ceil_out_strict(offerAmount, limit, roundUp);
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
        return quality().ceil_in_strict(offerAmount, limit, roundUp);
    }
    return m_quality.ceil_in(offerAmount, limit);
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
