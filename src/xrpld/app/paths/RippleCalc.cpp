#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/paths/RippleCalc.h>
#include <xrpld/app/paths/detail/FlowDebugInfo.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {
namespace path {

RippleCalc::Output
RippleCalc::rippleCalculate(
    PaymentSandbox& view,

    // Compute paths using this ledger entry set.  Up to caller to actually
    // apply to ledger.

    // Issuer:
    //      XRP: xrpAccount()
    //  non-XRP: uSrcAccountID (for any issuer) or another account with
    //           trust node.
    STAmount const& saMaxAmountReq,  // --> -1 = no limit.

    // Issuer:
    //      XRP: xrpAccount()
    //  non-XRP: uDstAccountID (for any issuer) or another account with
    //           trust node.
    STAmount const& saDstAmountReq,

    AccountID const& uDstAccountID,
    AccountID const& uSrcAccountID,

    // A set of paths that are included in the transaction that we'll
    // explore for liquidity.
    STPathSet const& spsPaths,

    std::optional<uint256> const& domainID,
    Logs& l,
    Input const* const pInputs)
{
    Output flowOut;
    PaymentSandbox flowSB(&view);
    auto j = l.journal("Flow");

    if (!view.rules().enabled(featureFlow))
    {
        // The new payment engine was enabled several years ago. New transaction
        // should never use the old rules. Assume this is a replay
        j.fatal()
            << "Old payment rules are required for this transaction. Assuming "
               "this is a replay and running with the new rules.";
    }

    {
        bool const defaultPaths =
            !pInputs ? true : pInputs->defaultPathsAllowed;

        bool const partialPayment =
            !pInputs ? false : pInputs->partialPaymentAllowed;

        auto const limitQuality = [&]() -> std::optional<Quality> {
            if (pInputs && pInputs->limitQuality &&
                saMaxAmountReq > beast::zero)
                return Quality{Amounts(saMaxAmountReq, saDstAmountReq)};
            return std::nullopt;
        }();

        auto const sendMax = [&]() -> std::optional<STAmount> {
            if (saMaxAmountReq >= beast::zero ||
                saMaxAmountReq.getCurrency() != saDstAmountReq.getCurrency() ||
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
                OfferCrossing::no,
                limitQuality,
                sendMax,
                domainID,
                j,
                nullptr);
        }
        catch (std::exception& e)
        {
            JLOG(j.error()) << "Exception from flow: " << e.what();

            // return a tec so the tx is stored
            path::RippleCalc::Output exceptResult;
            exceptResult.setResult(tecINTERNAL);
            return exceptResult;
        }
    }

    j.debug() << "RippleCalc Result> "
              << " actualIn: " << flowOut.actualAmountIn
              << ", actualOut: " << flowOut.actualAmountOut
              << ", result: " << flowOut.result()
              << ", dstAmtReq: " << saDstAmountReq
              << ", sendMax: " << saMaxAmountReq;

    flowSB.apply(view);
    return flowOut;
}

}  // namespace path
}  // namespace ripple
