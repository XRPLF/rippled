//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_APPLYIMPL_H_INCLUDED
#define RIPPLE_TX_APPLYIMPL_H_INCLUDED

namespace ripple
{

struct PreflightResult;
struct PreclaimResult;

/** Gate a transaction based on static information.

The transaction is checked against all possible
validity constraints that do not require a ledger.

@return A PreflightResult object constaining, among
other things, the TER code.
*/
PreflightResult
preflight(Application& app, Rules const& rules,
    STTx const& tx, ApplyFlags flags,
        beast::Journal j);

/** Gate a transaction based on static ledger information.

The transaction is checked against all possible
validity constraints that DO require a ledger.

If preclaim succeeds, then the transaction is very
likely to claim a fee. This will determine if the
transaction is safe to relay without being applied
to the open ledger.

"Succeeds" in this case is defined as returning a
`tes` or `tec`, since both lead to claiming a fee.

@return A PreclaimResult object containing, among
other things the TER code and the base fee value for
this transaction.
*/
PreclaimResult
preclaim(PreflightResult const& preflightResult,
    Application& app, OpenView const& view);

/** Apply a prechecked transaction to an OpenView.

See also: apply()

Precondition: The transaction has been checked
and validated using the above functions.

@return A pair with the TER and a bool indicating
whether or not the transaction was applied.
*/
std::pair<TER, bool>
doApply(PreclaimResult const& preclaimResult,
    Application& app, OpenView& view);

}

#endif
