#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <stdexcept>

namespace xrpl {

template <class TIn, class TOut>
class TOfferBase
{
protected:
    Issue issIn_;
    Issue issOut_;
};

template <>
class TOfferBase<STAmount, STAmount>
{
public:
    explicit TOfferBase() = default;
};

template <class TIn = STAmount, class TOut = STAmount>
class TOffer : private TOfferBase<TIn, TOut>
{
private:
    SLE::pointer entry_;
    Quality quality_;
    AccountID account_;

    TAmounts<TIn, TOut> amounts_;
    void
    setFieldAmounts();

public:
    TOffer() = default;

    TOffer(SLE::pointer const& entry, Quality quality);

    /** Returns the quality of the offer.
        Conceptually, the quality is the ratio of output to input currency.
        The implementation calculates it as the ratio of input to output
        currency (so it sorts ascending). The quality is computed at the time
        the offer is placed, and never changes for the lifetime of the offer.
        This is an important business rule that maintains accuracy when an
        offer is partially filled; Subsequent partial fills will use the
        original quality.
    */
    Quality
    quality() const noexcept
    {
        return quality_;
    }

    /** Returns the account id of the offer's owner. */
    AccountID const&
    owner() const
    {
        return account_;
    }

    /** Returns the in and out amounts.
        Some or all of the out amount may be unfunded.
    */
    TAmounts<TIn, TOut> const&
    amount() const
    {
        return amounts_;
    }

    /** Returns `true` if no more funds can flow through this offer. */
    bool
    fullyConsumed() const
    {
        if (amounts_.in <= beast::zero)
            return true;
        if (amounts_.out <= beast::zero)
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

    std::string
    id() const
    {
        return to_string(entry_->key());
    }

    std::optional<uint256>
    key() const
    {
        return entry_->key();
    }

    Issue const&
    issueIn() const;
    Issue const&
    issueOut() const;

    TAmounts<TIn, TOut>
    limitOut(TAmounts<TIn, TOut> const& offerAmount, TOut const& limit, bool roundUp) const;

    TAmounts<TIn, TOut>
    limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp) const;

    template <typename... Args>
    static TER
    send(Args&&... args);

    bool
    isFunded() const
    {
        // Offer owner is issuer; they have unlimited funds
        return account_ == issueOut().account;
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
    bool
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

using Offer = TOffer<>;

template <class TIn, class TOut>
TOffer<TIn, TOut>::TOffer(SLE::pointer const& entry, Quality quality)
    : entry_(entry), quality_(quality), account_(entry_->getAccountID(sfAccount))
{
    auto const tp = entry_->getFieldAmount(sfTakerPays);
    auto const tg = entry_->getFieldAmount(sfTakerGets);
    amounts_.in = toAmount<TIn>(tp);
    amounts_.out = toAmount<TOut>(tg);
    this->issIn_ = tp.issue();
    this->issOut_ = tg.issue();
}

template <>
inline TOffer<STAmount, STAmount>::TOffer(SLE::pointer const& entry, Quality quality)
    : entry_(entry)
    , quality_(quality)
    , account_(entry_->getAccountID(sfAccount))
    , amounts_(entry_->getFieldAmount(sfTakerPays), entry_->getFieldAmount(sfTakerGets))
{
}

template <class TIn, class TOut>
void
TOffer<TIn, TOut>::setFieldAmounts()
{
    // LCOV_EXCL_START
#ifdef _MSC_VER
    UNREACHABLE("xrpl::TOffer::setFieldAmounts : must be specialized");
#else
    static_assert(sizeof(TOut) == -1, "Must be specialized");
#endif
    // LCOV_EXCL_STOP
}

template <class TIn, class TOut>
TAmounts<TIn, TOut>
TOffer<TIn, TOut>::limitOut(TAmounts<TIn, TOut> const& offerAmount, TOut const& limit, bool roundUp)
    const
{
    // It turns out that the ceil_out implementation has some slop in
    // it, which ceil_out_strict removes.
    return quality().ceil_out_strict(offerAmount, limit, roundUp);
}

template <class TIn, class TOut>
TAmounts<TIn, TOut>
TOffer<TIn, TOut>::limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp)
    const
{
    if (auto const& rules = getCurrentTransactionRules();
        rules && rules->enabled(fixReducedOffersV2))
        // It turns out that the ceil_in implementation has some slop in
        // it.  ceil_in_strict removes that slop.  But removing that slop
        // affects transaction outcomes, so the change must be made using
        // an amendment.
        return quality().ceil_in_strict(offerAmount, limit, roundUp);
    return quality_.ceil_in(offerAmount, limit);
}

template <class TIn, class TOut>
template <typename... Args>
TER
TOffer<TIn, TOut>::send(Args&&... args)
{
    return accountSend(std::forward<Args>(args)...);
}

template <>
inline void
TOffer<STAmount, STAmount>::setFieldAmounts()
{
    entry_->setFieldAmount(sfTakerPays, amounts_.in);
    entry_->setFieldAmount(sfTakerGets, amounts_.out);
}

template <>
inline void
TOffer<IOUAmount, IOUAmount>::setFieldAmounts()
{
    entry_->setFieldAmount(sfTakerPays, toSTAmount(amounts_.in, issIn_));
    entry_->setFieldAmount(sfTakerGets, toSTAmount(amounts_.out, issOut_));
}

template <>
inline void
TOffer<IOUAmount, XRPAmount>::setFieldAmounts()
{
    entry_->setFieldAmount(sfTakerPays, toSTAmount(amounts_.in, issIn_));
    entry_->setFieldAmount(sfTakerGets, toSTAmount(amounts_.out));
}

template <>
inline void
TOffer<XRPAmount, IOUAmount>::setFieldAmounts()
{
    entry_->setFieldAmount(sfTakerPays, toSTAmount(amounts_.in));
    entry_->setFieldAmount(sfTakerGets, toSTAmount(amounts_.out, issOut_));
}

template <class TIn, class TOut>
Issue const&
TOffer<TIn, TOut>::issueIn() const
{
    return this->issIn_;
}

template <>
inline Issue const&
TOffer<STAmount, STAmount>::issueIn() const
{
    return amounts_.in.issue();
}

template <class TIn, class TOut>
Issue const&
TOffer<TIn, TOut>::issueOut() const
{
    return this->issOut_;
}

template <>
inline Issue const&
TOffer<STAmount, STAmount>::issueOut() const
{
    return amounts_.out.issue();
}

template <class TIn, class TOut>
inline std::ostream&
operator<<(std::ostream& os, TOffer<TIn, TOut> const& offer)
{
    return os << offer.id();
}

}  // namespace xrpl
