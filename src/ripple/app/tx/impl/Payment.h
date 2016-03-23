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

#ifndef RIPPLE_TX_PAYMENT_H_INCLUDED
#define RIPPLE_TX_PAYMENT_H_INCLUDED

#include <ripple/app/paths/RippleCalc.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

// See https://ripple.com/wiki/Transaction_Format#Payment_.280.29

class Payment
    : public Transactor
{
    /* The largest number of paths we allow */
    static std::size_t const MaxPathSize = 6;

    /* The longest path we allow */
    static std::size_t const MaxPathLength = 8;

public:
    Payment (ApplyContext& ctx)
        : Transactor(ctx)
    {
    }

    static
    XRPAmount
    calculateMaxSpend(STTx const& tx);

    static
    TER
    preflight (PreflightContext const& ctx);

    static
    TER
    preclaim(PreclaimContext const& ctx);

    TER doApply () override;
};

} // ripple

#endif
