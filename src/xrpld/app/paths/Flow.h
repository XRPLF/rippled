#ifndef XRPL_APP_PATHS_FLOW_H_INCLUDED
#define XRPL_APP_PATHS_FLOW_H_INCLUDED

#include <xrpld/app/paths/RippleCalc.h>
#include <xrpld/app/paths/detail/Steps.h>

#include <xrpl/protocol/Quality.h>

namespace ripple {

namespace path {
namespace detail {
struct FlowDebugInfo;
}
}  // namespace path

/**
  Make a payment from the src account to the dst account

  @param view Trust lines and balances
  @param deliver Amount to deliver to the dst account
  @param src Account providing input funds for the payment
  @param dst Account receiving the payment
  @param paths Set of paths to explore for liquidity
  @param defaultPaths Include defaultPaths in the path set
  @param partialPayment If the payment cannot deliver the entire
           requested amount, deliver as much as possible, given the constraints
  @param ownerPaysTransferFee If true then owner, not sender, pays fee
  @param offerCrossing If Yes or Sell then flow is executing offer crossing, not
  payments
  @param limitQuality Do not use liquidity below this quality threshold
  @param sendMax Do not spend more than this amount
  @param j Journal to write journal messages to
  @param flowDebugInfo If non-null a pointer to FlowDebugInfo for debugging
  @return Actual amount in and out, and the result code
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

}  // namespace ripple

#endif
