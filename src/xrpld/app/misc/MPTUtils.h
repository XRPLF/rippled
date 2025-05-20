//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_MPTUTILS_H_INLCUDED
#define RIPPLE_APP_MISC_MPTUTILS_H_INLCUDED

#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

class Asset;
class ReadView;

/* Return true if a transaction is allowed for the specified MPT/account. The
 * function checks MPTokenIssuance and MPToken objects flags to determine if the
 * transaction is allowed.
 */
TER
isMPTTxAllowed(
    ReadView const& v,
    TxType tx,
    Asset const& asset,
    AccountID const& accountID,
    std::optional<AccountID> const& destAccount = std::nullopt);

TER
isMPTDEXAllowed(
    ReadView const& view,
    Asset const& issuanceID,
    AccountID const& srcAccount,
    AccountID const& destAccount);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_MPTUTILS_H_INLCUDED
