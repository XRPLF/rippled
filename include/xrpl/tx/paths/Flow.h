/** @file
 *  Public entry point for the XRP Ledger payment flow engine.
 *
 *  Declares `flow()`, the single function that every Payment transaction,
 *  offer crossing, and check-cash operation calls to move funds through
 *  the ledger. The implementation resolves source/destination asset types,
 *  builds execution strands from the supplied path hints, and dispatches
 *  to a type-parameterised inner loop in `StrandFlow.h`.
 */
#pragma once

#include <xrpl/protocol/Quality.h>
#include <xrpl/tx/paths/RippleCalc.h>
#include <xrpl/tx/paths/detail/Steps.h>

namespace xrpl {

namespace path::detail {
/** Diagnostic trace populated by `flow()` during testing.
 *
 *  When a non-null pointer is passed to `flow()`, the inner strand-execution
 *  loop fills this structure with per-strand, per-step execution traces.
 *  In production the pointer is null and this path has zero overhead.
 */
struct FlowDebugInfo;
}  // namespace path::detail

/** Execute a payment through the path-finding and strand-execution engine.
 *
 *  Routes funds from `src` to `dst` using the candidate paths in `paths`
 *  (and optionally the default path). Strand construction, offer-book
 *  traversal, AMM liquidity, and trust-line balance updates are all staged
 *  inside `view`. The sandbox is mutated only when the result is
 *  `tesSUCCESS`; on any failure the sandbox is left pristine.
 *
 *  Source-asset inference: if `sendMax` is present its asset is used as the
 *  source asset. Otherwise, for IOU deliveries the source asset adopts `src`
 *  as its issuer (the "any issuer from src" semantic); for MPT and XRP the
 *  delivery asset is used directly.
 *
 *  @param view Mutable speculative ledger view. All balance and trust-line
 *      mutations are staged here. The caller owns the sandbox and decides
 *      whether to commit the result to the underlying view.
 *  @param deliver Target amount to deliver to `dst`. Determines the
 *      destination asset and, when `sendMax` is absent, the source asset.
 *  @param src Account supplying the input funds.
 *  @param dst Account receiving the delivered funds.
 *  @param paths Candidate path hints from the transaction's `sfPaths` field.
 *      Translated into `Strand` objects via `toStrands()`; an empty set is
 *      valid when `defaultPaths` is true.
 *  @param defaultPaths When true, the direct src→dst path is added to the
 *      strand set even if it does not appear in `paths`.
 *  @param partialPayment When true, the engine delivers as much as possible
 *      up to `deliver` rather than failing if the full amount cannot be
 *      routed. Corresponds to the `tfPartialPayment` transaction flag.
 *  @param ownerPaysTransferFee When true, IOU transfer fees are charged to
 *      the offer owner rather than the payment sender. Set for offer
 *      crossing; clear for normal payments.
 *  @param offerCrossing Distinguishes operational mode: `No` for Payment
 *      transactions, `Yes` or `Sell` for offer crossing. Affects fee
 *      attribution, quality constraints, and offer eligibility within each
 *      strand step.
 *  @param limitQuality Optional minimum acceptable exchange rate
 *      (output/input). Book steps stop consuming liquidity once the best
 *      available offer quality falls below this threshold. Used to enforce
 *      the taker's price constraint during offer crossing.
 *  @param sendMax Optional upper bound on the sender's spend. Its asset
 *      also drives source-asset inference when present.
 *  @param domainID Optional domain identifier for domain-scoped order books.
 *      When set, book lookups are restricted to the specified domain and
 *      threaded down into every `StrandContext` and `BookStep`.
 *  @param j Journal for diagnostic logging during strand execution.
 *  @param flowDebugInfo If non-null, the inner flow template populates this
 *      structure with per-strand execution traces for testing or diagnostics.
 *      Null in production; the debug path has zero overhead when null.
 *  @return A `RippleCalc::Output` containing: `actualAmountIn` (source
 *      spend), `actualAmountOut` (amount delivered), `removableOffers`
 *      (unfunded/expired offers discovered during traversal — populated even
 *      on failure so callers can clean up ledger hygiene), and `result()`
 *      (the `TER` outcome). On failure, only `removableOffers` and
 *      `result()` are meaningful; `actualAmountIn`/`Out` may be zero.
 *  @note If strand construction via `toStrands()` fails, the error `TER` is
 *      returned immediately and the sandbox is not touched.
 *  @see path::RippleCalc::rippleCalculate for the older pre-Flow path engine
 *      that shares the same `Output` type.
 */
path::RippleCalc::Output
flow(
    PaymentSandbox& view,
    STAmount const& deliver,
    AccountID const& src,
    AccountID const& dst,
    STPathSet const& paths,
    bool defaultPaths,
    bool partialPayment,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMax,
    std::optional<uint256> const& domainID,
    beast::Journal j,
    path::detail::FlowDebugInfo* flowDebugInfo = nullptr);

}  // namespace xrpl
