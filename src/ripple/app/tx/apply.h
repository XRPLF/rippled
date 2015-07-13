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

/** Gate a transaction based on static information.

    The transaction is checked against all possible
    validity constraints that do not require a ledger.

    @return The TER code (a `tem` or tesSUCCESS)
*/
TER
preflight (Rules const& rules, STTx const& tx,
    ApplyFlags flags,
        Config const& config, beast::Journal j);

/** Apply a prechecked transaction to an OpenView.

    See also: apply()

    Precondition: The transaction has been checked
    and validated using the above function(s).

    @return A pair with the TER and a bool indicating
            whether or not the transaction was applied.
*/
std::pair<TER, bool>
doapply(OpenView& view, STTx const& tx,
    ApplyFlags flags, Config const& config,
        beast::Journal j);

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
apply (OpenView& view,
    STTx const& tx, ApplyFlags flags,
        Config const& config,
            beast::Journal journal);

} // ripple

#endif
