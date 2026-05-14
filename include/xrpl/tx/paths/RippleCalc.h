/** @file
 *  Entry point for the XRPL payment path quality engine.
 *
 *  Declares `RippleCalc`, the adapter that orchestrates a speculative
 *  payment computation through the trust-line and order-book network.
 *  The `Payment` transactor is the primary caller; offer-crossing logic
 *  calls `flow()` directly.
 */

#pragma once

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

namespace xrpl {
class Config;
namespace path {

namespace detail {
struct FlowDebugInfo;
}  // namespace detail

/** Computes payment quality across the XRPL trust-line and order-book network.
 *
 *  "Quality" is the effective exchange rate for a payment: the input amount
 *  required to deliver a given output along a set of candidate paths.
 *
 *  The sole public interface is the static `rippleCalculate()` factory, which
 *  creates a nested `PaymentSandbox` over the caller's view, delegates to
 *  `flow()`, and commits the nested sandbox to the caller's view only on
 *  success. The caller retains full ownership of the outer sandbox and may
 *  discard it if the enclosing transaction fails.
 *
 *  `RippleCalc` objects are lightweight: they hold only a sandbox reference
 *  and the `permanentlyUnfundedOffers` set. No heap allocation is incurred
 *  for the common case.
 */
class RippleCalc
{
public:
    /** Behavioral knobs for a single `rippleCalculate()` invocation.
     *
     *  A null `Input` pointer passed to `rippleCalculate()` is valid and
     *  equivalent to the default-constructed values below.
     */
    struct Input
    {
        explicit Input() = default;

        /** Allow delivery of less than the full requested amount.
         *
         *  When true, the engine delivers as much as paths allow rather than
         *  failing with `tecPATH_PARTIAL` if the full amount cannot be routed.
         *  Corresponds to the `tfPartialPayment` transaction flag.
         */
        bool partialPaymentAllowed = false;

        /** Include the implicit direct sender→receiver path.
         *
         *  When true (the default), a direct path is added to the strand set
         *  even if it is absent from `sfPaths`. Set to false only when the
         *  caller wants to restrict routing to explicitly provided paths.
         */
        bool defaultPathsAllowed = true;

        /** Enforce a minimum acceptable exchange rate.
         *
         *  When true and `saMaxAmountReq` is positive, the engine derives a
         *  quality floor of `Amounts(saMaxAmountReq, saDstAmountReq)` and
         *  rejects liquidity below that rate. Prevents accepting a worse rate
         *  than the sender specified via `sfSendMax`.
         */
        bool limitQuality = false;

        /** Distinguish open-ledger from closed-ledger processing.
         *
         *  Passed through to downstream rounding and fee logic. Set to
         *  `view().open()` by the `Payment` transactor.
         */
        bool isLedgerOpen = true;
    };

    /** Results of a `rippleCalculate()` or `flow()` invocation. */
    struct Output
    {
        explicit Output() = default;

        /** Actual source amount consumed from the sender's account. */
        STAmount actualAmountIn;

        /** Actual amount delivered to the destination account. */
        STAmount actualAmountOut;

        /** Offers found to be expired or unfunded during path traversal.
         *
         *  On a successful payment these offers are deleted from the ledger as
         *  a side effect. On failure the ledger is not modified, but this set
         *  is still populated so that offer-crossing logic can clean up stale
         *  state independently of payment outcome.
         *
         *  The flat_set provides compact, ordered storage for deterministic
         *  iteration — consensus requires every validator to process the same
         *  offers in the same sequence.
         */
        boost::container::flat_set<uint256> removableOffers;

    private:
        TER calculationResult_ = temUNKNOWN;

    public:
        /** Return the TER outcome of the calculation. */
        [[nodiscard]] TER
        result() const
        {
            return calculationResult_;
        }

        /** Set the TER outcome of the calculation.
         *
         *  Controlled access prevents callers from accidentally overwriting
         *  an internally-set error code.
         *
         *  @param value The TER code to store.
         */
        void
        setResult(TER const value)
        {
            calculationResult_ = value;
        }
    };

    /** Compute the quality of a payment and stage ledger mutations speculatively.
     *
     *  Creates a nested `PaymentSandbox` over `view`, invokes `flow()` to walk
     *  paths and cross order books, then — on success — commits the nested
     *  sandbox back into `view`. On any failure `view` is left unmodified.
     *
     *  Any exception thrown by `flow()` is caught and converted to a
     *  `tecINTERNAL` result so the transaction is stored rather than silently
     *  dropped by the node.
     *
     *  @param view Caller-owned speculative ledger view. Mutations are staged
     *      here only when the return value carries `tesSUCCESS`. The caller
     *      decides whether to commit `view` to the underlying ledger.
     *  @param saMaxAmountReq Maximum amount the sender is willing to spend
     *      (`sfSendMax`). Pass -1 for no limit. For XRP the issuer must be
     *      `xrpAccount()`; for non-XRP assets the issuer is `uSrcAccountID`
     *      or another account with a trust node.
     *  @param saDstAmountReq Exact amount to deliver to `uDstAccountID`
     *      (`sfAmount`). For XRP the issuer must be `xrpAccount()`; for
     *      non-XRP assets the issuer is `uDstAccountID` or another account
     *      with a trust node.
     *  @param uDstAccountID Account that must receive `saDstAmountReq`.
     *  @param uSrcAccountID Account supplying the input funds.
     *  @param spsPaths Candidate path hints from the transaction's `sfPaths`
     *      field. An empty set is valid when `pInputs->defaultPathsAllowed`
     *      is true.
     *  @param domainID Optional domain identifier for domain-scoped order
     *      books. When set, liquidity is restricted to the specified
     *      permissioned domain.
     *  @param registry Service registry supplying cross-cutting services,
     *      including the journal obtained via `registry.getJournal("Flow")`.
     *  @param pInputs Optional behavioral flags. Null is treated as the
     *      conservative default: no partial payment, default paths enabled,
     *      no quality limit.
     *  @return An `Output` holding `actualAmountIn`, `actualAmountOut`,
     *      `removableOffers`, and a `TER` result. On failure only
     *      `removableOffers` and `result()` are meaningful.
     *  @note `sendMax` is derived internally: it is set to `saMaxAmountReq`
     *      unless the sender and issuer are identical and the source/destination
     *      assets match, in which case `sendMax` is `nullopt` (no separate
     *      spending cap needed).
     */
    static Output
    rippleCalculate(
        PaymentSandbox& view,
        STAmount const& saMaxAmountReq,
        STAmount const& saDstAmountReq,
        AccountID const& uDstAccountID,
        AccountID const& uSrcAccountID,
        STPathSet const& spsPaths,
        std::optional<uint256> const& domainID,
        ServiceRegistry& registry,
        Input const* const pInputs = nullptr);

    /** Mutable speculative ledger view used during path computation. */
    PaymentSandbox& view;

    /** Offers that must be removed from the ledger regardless of payment outcome.
     *
     *  Tracks structurally unfunded offers (as opposed to temporarily
     *  underfunded ones). Removal is performed in a deterministic order using
     *  this ordered container so that every validator produces identical ledger
     *  mutations and agrees on the resulting hash.
     */
    boost::container::flat_set<uint256> permanentlyUnfundedOffers;
};

}  // namespace path
}  // namespace xrpl
