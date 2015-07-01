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

/** Apply a transaction to a BasicView.

    Throws:
        
        Exceptions are thrown on broken invariants. Callers
        should catch these exceptions to protect the ledger
        and the running process.

        std::logic_error
        (any)

    @return A pair with the TER and a bool indicating
            whether or not the transaction was applied.
*/
// VFALCO Some call sites use try/catch some don't.
std::pair<TER, bool>
apply (BasicView& view,
    std::shared_ptr<STTx const> const& tx,
        ViewFlags flags,
            Config const& config,
                beast::Journal journal);

} // ripple

#endif
