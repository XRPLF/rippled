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

#ifndef RIPPLE_APP_MISC_DELEGATEUTILS_H_INCLUDED
#define RIPPLE_APP_MISC_DELEGATEUTILS_H_INCLUDED

#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace ripple {

/**
 * Check if the delegate account has permission to execute the transaction.
 * @param delegate The delegate account.
 * @param tx The transaction that the delegate account intends to execute.
 * @return tesSUCCESS if the transaction is allowed, terNO_DELEGATE_PERMISSION
 * if not.
 */
NotTEC
checkTxPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx);

/**
 * Load the granular permissions granted to the delegate account for the
 * specified transaction type
 * @param delegate The delegate account.
 * @param type Used to determine which granted granular permissions to load,
 * based on the transaction type.
 * @param granularPermissions Granted granular permissions tied to the
 * transaction type.
 */
void
loadGranularPermission(
    std::shared_ptr<SLE const> const& delegate,
    TxType const& type,
    std::unordered_set<GranularPermissionType>& granularPermissions);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_DELEGATEUTILS_H_INCLUDED
