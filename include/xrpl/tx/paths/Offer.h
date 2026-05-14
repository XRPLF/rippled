/** @file
 *  Defines `TOffer`, the typed CLOB offer wrapper used by the payment-path
 *  engine to read, limit, and consume Central Limit Order Book entries.
 *
 *  @see AMMOffer, BookStep, TOfferStreamBase
 */

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

/** Typed wrapper around a CLOB offer ledger entry for the payment-path engine.
 *
 *  `TOffer` bridges a raw `SLE` (Shared Ledger Entry) and the generic
 *  `BookStep` template, providing a clean typed interface for reading offer
 *  amounts, applying partial fills, and routing funds.  Template parameters
 *  `TIn` and `TOut` — constrained to `XRPAmount`, `IOUAmount`, or `MPTAmount`
 *  — let a single class body handle every asset-type combination while
 *  permitting compile-time dispatch where serialization paths differ.
 *
 *  `TOffer` and `AMMOffer` expose the same named interface so that `BookStep`
 *  can treat CLOB and AMM liquidity polymorphically via structural duck-typing
 *  without virtual dispatch.
 *
 *  @tparam TIn  Amount type for the input (TakerPays) side of the offer.
 *      Must satisfy the `StepAmount` concept (`XRPAmount`, `IOUAmount`, or
 *      `MPTAmount`).
 *  @tparam TOut Amount type for the output (TakerGets) side of the offer.
 *      Must satisfy the `StepAmount` concept.
 *
 *  @note After construction the object is self-contained — it holds copies of
 *      all relevant amounts and asset identities extracted from the `SLE`.
 *      The `SLE` itself is not read again until `consume()` writes back the
 *      updated amounts.
 *
 *  @see AMMOffer, BookStep, TOfferStreamBase
 */
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

    /** Write the current `amounts_` back into the underlying `SLE` fields.
     *
     *  Uses `if constexpr` to select between the XRP path (`toSTAmount(amount)`
     *  with no asset context) and the IOU/MPT path (`toSTAmount(amount, asset_)`)
     *  at compile time, avoiding runtime polymorphism while sharing the body.
     *  Called only from `consume()`.
     */
    void
    setFieldAmounts();

public:
    TOffer() = default;

    /** Construct from a ledger entry and its pre-computed quality.
     *
     *  Reads `sfTakerPays` and `sfTakerGets` from `entry` and converts them
     *  to the strongly-typed `TIn`/`TOut` amounts via `toAmount<T>()`.  Asset
     *  identities are captured from the `STAmount::asset()` accessors.
     *
     *  @param entry    Shared pointer to the offer's `SLE`.  Must not be null.
     *  @param quality  Pre-computed quality for this offer as stored in the
     *      order book page; not recalculated here.
     */
    TOffer(SLE::pointer entry, Quality quality);

    /** Returns the quality of the offer.
     *
     *  Conceptually the quality is the ratio of output to input currency.
     *  Internally it is stored as input-to-output (ascending integer order
     *  maps to descending quality) so that the order book's sort order is
     *  stable.
     *
     *  Quality is fixed at the moment the offer is placed and never
     *  recalculated, even after partial fills.  This is a deliberate ledger
     *  invariant: partial fills reduce only the absolute amounts, leaving the
     *  exchange rate unchanged and preventing accumulated rounding drift from
     *  silently worsening the effective rate for later takers.
     *
     *  @return The offer's immutable quality.
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

    /** Returns the remaining in/out amounts for this offer.
     *
     *  The out amount reflects what is recorded in the ledger entry; some or
     *  all of it may be unfunded if the owner's balance has dropped since the
     *  offer was placed.  `TOfferStreamBase` verifies actual owner funds via
     *  `ownerFunds_` before crossing.
     *
     *  @return Reference to the `{in, out}` pair; valid for the lifetime of
     *      this `TOffer`.
     */
    [[nodiscard]] TAmounts<TIn, TOut> const&
    amount() const
    {
        return amounts_;
    }

    /** Returns `true` if no more funds can flow through this offer.
     *
     *  The offer is considered fully consumed when either the input or output
     *  side has reached zero.  `BookStep` uses this to decide whether to erase
     *  the offer from the ledger after crossing.
     *
     *  @return `true` if `amounts_.in <= 0` or `amounts_.out <= 0`.
     */
    [[nodiscard]] bool
    fullyConsumed() const
    {
        if (amounts_.in <= beast::kZERO)
            return true;
        if (amounts_.out <= beast::kZERO)
            return true;
        return false;
    }

    /** Applies a partial or full consumption to this offer and stages the
     *  update in the ledger view.
     *
     *  Decrements `amounts_` by `consumed`, writes the updated values back
     *  into the `SLE` via `setFieldAmounts()`, and calls `view.update(entry_)`
     *  to stage the change in the `ApplyView`.
     *
     *  @param view      Mutable ledger view that will receive the updated SLE.
     *  @param consumed  The `{in, out}` amounts actually transferred.  Must
     *      not exceed the current `amounts_` in either dimension.
     *  @throws std::logic_error if `consumed.in > amounts_.in` or
     *      `consumed.out > amounts_.out`.  The calling code in `BookStep`
     *      is expected to clamp consumption first via `limitOut`/`limitIn`.
     */
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

    /** Returns the ledger-object key as a hex string, for logging.
     *
     *  @return String representation of the offer's 256-bit ledger key.
     */
    [[nodiscard]] std::string
    id() const
    {
        return to_string(entry_->key());
    }

    /** Returns the 256-bit ledger key of the underlying offer SLE.
     *
     *  `BookStep` uses this key to erase fully-consumed offers from the ledger.
     *  Unlike `AMMOffer::key()`, which always returns `std::nullopt`, this
     *  always returns a value for a valid `TOffer`.
     *
     *  @return The offer's ledger object key.
     */
    [[nodiscard]] std::optional<uint256>
    key() const
    {
        return entry_->key();
    }

    /** Returns the input-side asset of this offer.
     *
     *  @return Reference to the `Asset` captured from `sfTakerPays` at
     *      construction; valid for the lifetime of this `TOffer`.
     */
    [[nodiscard]] Asset const&
    assetIn() const;

    /** Returns the output-side asset of this offer.
     *
     *  @return Reference to the `Asset` captured from `sfTakerGets` at
     *      construction; valid for the lifetime of this `TOffer`.
     */
    [[nodiscard]] Asset const&
    assetOut() const;

    /** Clamps the offer to deliver at most `limit` units of the output asset.
     *
     *  Always delegates to `Quality::ceilOutStrict()`, which uses a tighter
     *  rounding algorithm than the older `ceilOut()` to remove slop that
     *  could keep offers alive longer than they should be.  Unlike `limitIn()`,
     *  the strict ceiling is unconditional — it was deployed before
     *  `fixReducedOffersV2` and does not require an amendment gate.
     *
     *  @param offerAmount  Current offer size used for proportional scaling.
     *  @param limit        Maximum output amount this offer may deliver.
     *  @param roundUp      Whether to round the computed input side up.
     *  @return Resized `{in, out}` pair where `out <= limit`.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    limitOut(TAmounts<TIn, TOut> const& offerAmount, TOut const& limit, bool roundUp) const;

    /** Clamps the offer to consume at most `limit` units of the input asset.
     *
     *  When the `fixReducedOffersV2` amendment is active, delegates to
     *  `Quality::ceilInStrict()`, which removes a small rounding slop present
     *  in the older `ceilIn()`.  The stricter ceiling changes observable
     *  transaction outcomes (it can prevent tiny residual amounts from keeping
     *  an offer alive), so it is gated behind the amendment to preserve replay
     *  of historical ledgers.  Without the amendment, falls back to
     *  `quality_.ceilIn()`.
     *
     *  @note The asymmetry with `limitOut()` — which is always strict — reflects
     *      the order in which these fixes were deployed on the network.
     *
     *  @param offerAmount  Current offer size used for proportional scaling.
     *  @param limit        Maximum input amount the taker will supply.
     *  @param roundUp      Whether to round the computed output side up (only
     *      forwarded when `fixReducedOffersV2` is active).
     *  @return Resized `{in, out}` pair where `in <= limit`.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp) const;

    /** Transfers funds from the offer owner, charging the issuer's transfer fee.
     *
     *  Delegates to `accountSend` with `WaiveTransferFee::No`, meaning CLOB
     *  offer owners pay the output asset issuer's transfer fee on each crossing.
     *  This is in contrast to `AMMOffer::send()`, which passes
     *  `WaiveTransferFee::Yes` because AMM pools are exempt from transfer fees
     *  under the protocol rules.
     *
     *  @param args  Arguments forwarded verbatim to `accountSend`.
     *  @return The `TER` result of `accountSend`.
     */
    template <typename... Args>
    static TER
    send(Args&&... args);

    /** Returns `true` when the offer owner is also the output-asset issuer.
     *
     *  An IOU issuer can deliver their own currency without holding a balance,
     *  so the path engine can bypass the normal `ownerFunds_` balance check for
     *  such offers.  Returns `false` for MPT and XRP output assets because
     *  issuers have no special delivery privilege for those types.
     *
     *  @return `true` only when `account_ == assetOut_.getIssuer()` and the
     *      output asset is an `Issue` (IOU); `false` otherwise.
     */
    [[nodiscard]] bool
    isFunded() const
    {
        // Offer owner is issuer; they have unlimited funds if IOU
        return account_ == assetOut_.getIssuer() && assetOut_.holds<Issue>();
    }

    /** Returns the in/out transfer-fee rates unchanged.
     *
     *  CLOB offer owners pay both the input-side and output-side transfer fees,
     *  so both rates are returned as-is.  This is in contrast to
     *  `AMMOffer::adjustRates()`, which zeroes the output-side rate to
     *  `QUALITY_ONE` because AMM swaps on Payment transactions are exempt from
     *  output-side transfer fees.
     *
     *  @param ofrInRate   Transfer fee rate on the input asset.
     *  @param ofrOutRate  Transfer fee rate on the output asset.
     *  @return `{ofrInRate, ofrOutRate}` — both rates unchanged.
     */
    static std::pair<std::uint32_t, std::uint32_t>
    adjustRates(std::uint32_t ofrInRate, std::uint32_t ofrOutRate)
    {
        // CLOB offer pays the transfer fee
        return {ofrInRate, ofrOutRate};
    }

    /** Verifies that the consumed amounts do not exceed the available amounts.
     *
     *  Gated on the `fixAMMv1_3` amendment.  For well-behaved callers this is
     *  always a no-op because `consume()` already enforces the same constraint
     *  — the check exists so `BookStep::consumeOffer()` can invoke it
     *  uniformly across both `TOffer` and `AMMOffer` (the AMM version performs
     *  a far more expensive constant-product pool invariant check).
     *
     *  @note The failure branch is marked `LCOV_EXCL_START`; it is considered
     *      unreachable under normal test coverage and exists purely as a
     *      defense-in-depth guard.
     *
     *  @param consumed  Amounts actually consumed in the trade.
     *  @param j         Journal for error-level diagnostics on invariant failure.
     *  @return `true` if `consumed` does not exceed `amounts_` in either
     *      dimension (or if the amendment is inactive); `false` otherwise.
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

/** Streams a `TOffer` to an output stream by its ledger-object key string.
 *
 *  @param os     The output stream.
 *  @param offer  The offer to stream.
 *  @return `os` after writing the offer's key string.
 */
template <StepAmount TIn, StepAmount TOut>
inline std::ostream&
operator<<(std::ostream& os, TOffer<TIn, TOut> const& offer)
{
    return os << offer.id();
}

}  // namespace xrpl
