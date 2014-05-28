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

#ifndef RIPPLE_RIPPLECALC_H
#define RIPPLE_RIPPLECALC_H

namespace ripple {
namespace path {

/** Calculate the quality of a payment path.

    The quality is a synonym for price. Specifically, the amount of
    input required to produce a given output along a specified path.
*/

TER rippleCalculate (
    LedgerEntrySet&                   lesActive,
    STAmount&                         saMaxAmountAct,
    STAmount&                         saDstAmountAct,
    PathState::List&                  pathStateList,
    const STAmount&                   saDstAmountReq,
    const STAmount&                   saMaxAmountReq,
    const uint160&                    uDstAccountID,
    const uint160&                    uSrcAccountID,
    const STPathSet&                  spsPaths,
    const bool                        bPartialPayment,
    const bool                        bLimitQuality,
    const bool                        bNoRippleDirect,
    // --> True, not to affect accounts.
    const bool                        bStandAlone,
    // --> What kind of errors to return.
    const bool                        bOpenLedger = true
);

} // path
} // ripple

#endif
