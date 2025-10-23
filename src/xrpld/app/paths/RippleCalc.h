#ifndef XRPL_APP_PATHS_RIPPLECALC_H_INCLUDED
#define XRPL_APP_PATHS_RIPPLECALC_H_INCLUDED

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

namespace ripple {
class Config;
namespace path {

namespace detail {
struct FlowDebugInfo;
}

/** RippleCalc calculates the quality of a payment path.

    Quality is the amount of input required to produce a given output along a
    specified path - another name for this is exchange rate.
*/
class RippleCalc
{
public:
    struct Input
    {
        explicit Input() = default;

        bool partialPaymentAllowed = false;
        bool defaultPathsAllowed = true;
        bool limitQuality = false;
        bool isLedgerOpen = true;
    };
    struct Output
    {
        explicit Output() = default;

        // The computed input amount.
        STAmount actualAmountIn;

        // The computed output amount.
        STAmount actualAmountOut;

        // Collection of offers found expired or unfunded. When a payment
        // succeeds, unfunded and expired offers are removed. When a payment
        // fails, they are not removed. This vector contains the offers that
        // could have been removed but were not because the payment fails. It is
        // useful for offer crossing, which does remove the offers.
        boost::container::flat_set<uint256> removableOffers;

    private:
        TER calculationResult_ = temUNKNOWN;

    public:
        TER
        result() const
        {
            return calculationResult_;
        }
        void
        setResult(TER const value)
        {
            calculationResult_ = value;
        }
    };

    static Output
    rippleCalculate(
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
        Input const* const pInputs = nullptr);

    // The view we are currently working on
    PaymentSandbox& view;

    // If the transaction fails to meet some constraint, still need to delete
    // unfunded offers in a deterministic order (hence the ordered container).
    //
    // Offers that were found unfunded.
    boost::container::flat_set<uint256> permanentlyUnfundedOffers_;
};

}  // namespace path
}  // namespace ripple

#endif
