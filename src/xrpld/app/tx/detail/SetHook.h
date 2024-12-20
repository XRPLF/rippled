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

#include <xrpld/app/hook/Enum.h>
#include <xrpld/app/hook/applyHook.h>
#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/tx/detail/SignerEntries.h>
#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <algorithm>
#include <cstdint>
#include <vector>

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

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

private:
    TER
    setHook();

    TER
    destroyNamespace(
        SetHookCtx& ctx,
        ApplyView& view,
        const AccountID& account,
        uint256 ns);

    TER
    removeHookFromLedger(
        Application& app,
        ApplyView& view,
        Keylet const& accountKeylet,
        Keylet const& ownerDirKeylet,
        Keylet const& hookKeylet);
};

}  // namespace ripple

#endif
