//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_PATHS_FLOW_H_INCLUDED
#define RIPPLE_APP_PATHS_FLOW_H_INCLUDED

#include <ripple/app/paths/RippleCalc.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/protocol/Quality.h>

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
  @param offerCrossing If true then flow is executing offer crossing, not
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
    bool offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMax,
    beast::Journal j,
    path::detail::FlowDebugInfo* flowDebugInfo = nullptr);

}  // namespace ripple

#endif
