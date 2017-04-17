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

#ifndef RIPPLE_TX_APPLY_H_INCLUDED
#define RIPPLE_TX_APPLY_H_INCLUDED

#include <ripple/core/Config.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TER.h>
#include <ripple/beast/utility/Journal.h>
#include <memory>
#include <utility>

namespace ripple {

class Application;
class HashRouter;

/** Describes the pre-processing validity of a transaction.

    @see checkValidity, forceValidity
*/
enum class Validity
{
    /// Signature is bad. Didn't do local checks.
    SigBad,
    /// Signature is good, but local checks fail.
    SigGoodOnly,
    /// Signature and local checks are good / passed.
    Valid
};

/** Checks transaction signature and local checks.

    @return A `Validity` enum representing how valid the
        `STTx` is and, if not `Valid`, a reason string.

    @note Results are cached internally, so tests will not be
        repeated over repeated calls, unless cache expires.

    @return `std::pair`, where `.first` is the status, and
            `.second` is the reason if appropriate.

    @see Validity
*/
std::pair<Validity, std::string>
checkValidity(HashRouter& router,
    STTx const& tx, Rules const& rules,
        Config const& config);


/** Sets the validity of a given transaction in the cache.

    @warning Use with extreme care.

    @note Can only raise the validity to a more valid state,
          and can not override anything cached bad.

    @see checkValidity, Validity
*/
void
forceValidity(HashRouter& router, uint256 const& txid,
    Validity validity);

/** Apply a transaction to an `OpenView`.

    This function is the canonical way to apply a transaction
    to a ledger. It rolls the validation and application
    steps into one function. To do the steps manually, the
    correct calling order is:
    @code{.cpp}
    preflight -> preclaim -> doApply
    @endcode
    The result of one function must be passed to the next.
    The `preflight` result can be safely cached and reused
    asynchronously, but `preclaim` and `doApply` must be called
    in the same thread and with the same view.

    @note Does not throw.

    For open ledgers, the `Transactor` will catch exceptions
    and return `tefEXCEPTION`. For closed ledgers, the
    `Transactor` will attempt to only charge a fee,
    and return `tecFAILED_PROCESSING`.

    If the `Transactor` gets an exception while trying
    to charge the fee, it will be caught and
    turned into `tefEXCEPTION`.

    For network health, a `Transactor` makes its
    best effort to at least charge a fee if the
    ledger is closed.

    @param app The current running `Application`.
    @param view The open ledger that the transaction
        will attempt to be applied to.
    @param tx The transaction to be checked.
    @param flags `ApplyFlags` describing processing options.
    @param journal A journal.

    @see preflight, preclaim, doApply

    @return A pair with the `TER` and a `bool` indicating
            whether or not the transaction was applied.
*/
std::pair<TER, bool>
apply (Application& app, OpenView& view,
    STTx const& tx, ApplyFlags flags,
        beast::Journal journal);


/** Enum class for return value from `applyTransaction`

    @see applyTransaction
*/
enum class ApplyResult
{
    /// Applied to this ledger
    Success,
    /// Should not be retried in this ledger
    Fail,
    /// Should be retried in this ledger
    Retry
};

/** Transaction application helper

    Provides more detailed logging and decodes the
    correct behavior based on the `TER` type

    @see ApplyResult
*/
ApplyResult
applyTransaction(Application& app, OpenView& view,
    STTx const& tx, bool retryAssured, ApplyFlags flags,
    beast::Journal journal);

} // ripple

#endif
