/** @file
 *  Implements `RippleCalc::rippleCalculate()`, the canonical entry point for
 *  executing a payment across a set of candidate paths.
 *
 *  The central design is the *double-sandbox* pattern: `rippleCalculate()`
 *  wraps the caller's `PaymentSandbox` in a second nested sandbox that is
 *  passed to `flow()`. If `flow()` throws, the nested sandbox is destroyed
 *  with its mutations unapplied, leaving the caller's view completely
 *  unmodified. The nested sandbox is committed to the caller's view only
 *  after `flow()` returns successfully.
 *
 *  Any uncaught exception from `flow()` is converted to `tecINTERNAL` so
 *  the transaction is stored in the ledger rather than silently dropped.
 *  This ensures all validators reach consensus on the same outcome even
 *  when one of them encounters an unexpected error condition.
 */

#include <xrpl/tx/paths/RippleCalc.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/paths/Flow.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <exception>
#include <optional>

namespace xrpl::path {

/** Execute a payment across candidate paths and stage ledger mutations.
 *
 *  Wraps `view` in a nested `PaymentSandbox`, delegates to `flow()` for
 *  multi-path traversal and liquidity aggregation, then commits the nested
 *  sandbox into `view` only after `flow()` returns. This double-sandbox
 *  pattern ensures that if `flow()` throws, `view` is left completely
 *  unmodified.
 *
 *  `sendMax` is derived internally from `saMaxAmountReq`: it is `nullopt`
 *  when the source and destination assets are identical and `uSrcAccountID`
 *  is the issuer, signalling to `flow()` that no separate spending cap is
 *  needed. In all other cases `sendMax` is set to `saMaxAmountReq`.
 *
 *  This function always invokes `flow()` in pure-payment mode
 *  (`ownerPaysTransferFee = false`, `OfferCrossing::No`). Offer-crossing
 *  calls `flow()` directly and does not go through `rippleCalculate()`.
 *
 *  @param view Caller-owned speculative view; mutations are committed here
 *      only when the returned `Output` carries `tesSUCCESS`.
 *  @param saMaxAmountReq Maximum amount the sender will spend (`sfSendMax`).
 *      Pass -1 for no limit.
 *  @param saDstAmountReq Exact amount to deliver to `uDstAccountID`
 *      (`sfAmount`).
 *  @param uDstAccountID Destination account.
 *  @param uSrcAccountID Source account supplying funds.
 *  @param spsPaths Candidate path hints from the transaction's `sfPaths`
 *      field.
 *  @param domainID Optional permissioned-domain scope for order-book
 *      liquidity. When set, only offers within the domain are eligible.
 *  @param registry Service registry; used to obtain the "Flow" journal.
 *  @param pInputs Optional behavioral flags (partial payment, default paths,
 *      quality limit). Null is treated as conservative defaults.
 *  @return An `Output` with `actualAmountIn`, `actualAmountOut`,
 *      `removableOffers`, and a `TER`. On exception, returns `tecINTERNAL`
 *      so the transaction is stored and all validators reach the same result.
 */
RippleCalc::Output
RippleCalc::rippleCalculate(
    PaymentSandbox& view,
    STAmount const& saMaxAmountReq,
    STAmount const& saDstAmountReq,
    AccountID const& uDstAccountID,
    AccountID const& uSrcAccountID,
    STPathSet const& spsPaths,
    std::optional<uint256> const& domainID,
    ServiceRegistry& registry,
    Input const* const pInputs)
{
    Output flowOut;
    PaymentSandbox flowSB(&view);
    auto j = registry.getJournal("Flow");

    {
        bool const defaultPaths = (pInputs == nullptr) ? true : pInputs->defaultPathsAllowed;

        bool const partialPayment = (pInputs == nullptr) ? false : pInputs->partialPaymentAllowed;

        auto const limitQuality = [&]() -> std::optional<Quality> {
            if (pInputs && pInputs->limitQuality && saMaxAmountReq > beast::kZERO)
                return Quality{Amounts(saMaxAmountReq, saDstAmountReq)};
            return std::nullopt;
        }();

        auto const sendMax = [&]() -> std::optional<STAmount> {
            if (saMaxAmountReq >= beast::kZERO ||
                !equalTokens(saMaxAmountReq.asset(), saDstAmountReq.asset()) ||
                saMaxAmountReq.getIssuer() != uSrcAccountID)
            {
                return saMaxAmountReq;
            }
            return std::nullopt;
        }();

        try
        {
            flowOut = flow(
                flowSB,
                saDstAmountReq,
                uSrcAccountID,
                uDstAccountID,
                spsPaths,
                defaultPaths,
                partialPayment,
                false,
                OfferCrossing::No,
                limitQuality,
                sendMax,
                domainID,
                j,
                nullptr);
        }
        catch (std::exception& e)
        {
            JLOG(j.error()) << "Exception from flow: " << e.what();
            path::RippleCalc::Output exceptResult;
            exceptResult.setResult(tecINTERNAL);
            return exceptResult;
        }
    }

    j.debug() << "RippleCalc Result> "
              << " actualIn: " << flowOut.actualAmountIn
              << ", actualOut: " << flowOut.actualAmountOut << ", result: " << flowOut.result()
              << ", dstAmtReq: " << saDstAmountReq << ", sendMax: " << saMaxAmountReq;

    flowSB.apply(view);
    return flowOut;
}

}  // namespace xrpl::path
