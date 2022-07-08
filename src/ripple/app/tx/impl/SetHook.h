//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_SETHOOK_H_INCLUDED
#define RIPPLE_TX_SETHOOK_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/Buffer.h>
#include <ripple/basics/Blob.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <ripple/app/hook/Enum.h>
#include <ripple/app/hook/applyHook.h>

namespace ripple {


struct SetHookCtx
{
    beast::Journal j;
    STTx const& tx;
    Application& app;
};

class SetHook : public Transactor
{

public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit SetHook(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    affectsSubsequentTransactionAuth(STTx const& tx)
    {
        return true;
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;

    void
    preCompute() override;

    static TER
    preclaim(PreclaimContext const&);

    static FeeUnit64
    calculateBaseFee(ReadView const& view, STTx const& tx);

private:

    TER
    setHook();

    TER
    destroyNamespace(
        SetHookCtx& ctx,
        ApplyView& view,
        const AccountID& account,
        uint256 ns
    );

    TER
    removeHookFromLedger(
        Application& app,
        ApplyView& view,
        Keylet const& accountKeylet,
        Keylet const& ownerDirKeylet,
        Keylet const& hookKeylet
    );

};

}  // namespace ripple

#endif
