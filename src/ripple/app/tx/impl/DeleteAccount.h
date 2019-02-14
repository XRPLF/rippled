//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_DELETEACCOUNT_H_INCLUDED
#define RIPPLE_TX_DELETEACCOUNT_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

class DeleteAccount
    : public Transactor
{
public:
    // Set a reasonable upper limit on the number of deletable directory
    // entries an account may have before we decide the account can't be
    // deleted.
    //
    // A limit is useful because if we go much past this limit the
    // transaction will fail anyway due to too much metadata (tecOVERSIZE).
    static constexpr std::int32_t maxDeletableDirEntries {1000};

    explicit DeleteAccount (ApplyContext& ctx)
        : Transactor(ctx)
    {
    }

    static
    bool
    affectsSubsequentTransactionAuth(STTx const& tx)
    {
        return true;
    }

    static
    NotTEC
    preflight (PreflightContext const& ctx);

    static
    FeeUnit64
    calculateBaseFee (
        ReadView const& view,
        STTx const& tx);

    static
    TER
    preclaim (PreclaimContext const& ctx);

    TER doApply () override;
};

}

#endif
