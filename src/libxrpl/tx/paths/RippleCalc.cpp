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
            if (pInputs && pInputs->limitQuality && saMaxAmountReq > beast::kZero)
                return Quality{Amounts(saMaxAmountReq, saDstAmountReq)};
            return std::nullopt;
        }();

        auto const sendMax = [&]() -> std::optional<STAmount> {
            if (saMaxAmountReq >= beast::kZero ||
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

            // return a tec so the tx is stored
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
