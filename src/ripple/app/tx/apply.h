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
#include <beast/utility/Journal.h>
#include <memory>
#include <utility>

namespace ripple {

class Application;
class HashRouter;

enum class Validity
{
    SigBad,         // Signature is bad. Didn't do local checks.
    SigGoodOnly,    // Signature is good, but local checks fail.
    Valid           // Signature and local checks are good / passed.
};

/** Checks transaction signature and local checks. Returns
    a Validity enum representing how valid the STTx is
    and, if not Valid, a reason string.
    Results are cached internally, so tests will not be
    repeated over repeated calls, unless cache expires.

    @return std::pair, where `.first` is the status, and
            `.second` is the reason if appropriate.
*/
std::pair<Validity, std::string>
checkValidity(HashRouter& router,
    STTx const& tx,
        bool allowMultiSign);

/** Checks transaction signature and local checks. Returns
    a Validity enum representing how valid the STTx is
    and, if not Valid, a reason string.
    Results are cached internally, so tests will not be
    repeated over repeated calls, unless cache expires.

    @return std::pair, where `.first` is the status, and
            `.second` is the reason if appropriate.
*/
std::pair<Validity, std::string>
checkValidity(HashRouter& router,
    STTx const& tx, Rules const& rules,
        Config const& config,
            ApplyFlags const& flags = tapNONE);


/** Sets the validity of a given transaction in the cache.
    Use with extreme care.

    @note Can only raise the validity to a more valid state,
          and can not override anything cached bad.
*/
void
forceValidity(HashRouter& router, uint256 const& txid,
    Validity validity);

/** Apply a transaction to a ReadView.

    Throws:
        Does not throw.

        For open ledgers, the Transactor will catch and
        return tefEXCEPTION. For closed ledgers, the
        Transactor will attempt to only charge a fee,
        and return tecFAILED_PROCESSING.

        If the Transactor gets an exception while trying
        to charge the fee, it will be caught here and
        turned into tefEXCEPTION.

        This try/catch handler is the last resort, any
        uncaught exceptions will be turned into
        tefEXCEPTION.

        For network health, a Transactor makes its
        best effort to at least charge a fee if the
        ledger is closed.

    @return A pair with the TER and a bool indicating
            whether or not the transaction was applied.
*/
std::pair<TER, bool>
apply (Application& app, OpenView& view,
    STTx const& tx, ApplyFlags flags,
        beast::Journal journal);

} // ripple

#endif
