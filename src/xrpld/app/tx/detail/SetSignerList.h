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

#ifndef RIPPLE_TX_SETSIGNERLIST_H_INCLUDED
#define RIPPLE_TX_SETSIGNERLIST_H_INCLUDED

#include <xrpld/app/tx/detail/SignerEntries.h>
#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STTx.h>

#include <cstdint>
#include <vector>

namespace ripple {

/**
See the README.md for an overview of the SetSignerList transaction that
this class implements.
*/
class SetSignerList : public Transactor
{
private:
    // Values determined during preCompute for use later.
    enum Operation { unknown, set, destroy };
    Operation do_{unknown};
    std::uint32_t quorum_{0};
    std::vector<SignerEntries::SignerEntry> signers_;

public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit SetSignerList(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;
    void
    preCompute() override;

    // Interface used by DeleteAccount
    static TER
    removeFromLedger(
        Application& app,
        ApplyView& view,
        AccountID const& account,
        beast::Journal j);

private:
    static std::tuple<
        NotTEC,
        std::uint32_t,
        std::vector<SignerEntries::SignerEntry>,
        Operation>
    determineOperation(STTx const& tx, ApplyFlags flags, beast::Journal j);

    static NotTEC
    validateQuorumAndSignerEntries(
        std::uint32_t quorum,
        std::vector<SignerEntries::SignerEntry> const& signers,
        AccountID const& account,
        beast::Journal j,
        Rules const&);

    TER
    replaceSignerList();
    TER
    destroySignerList();

    void
    writeSignersToSLE(SLE::pointer const& ledgerEntry, std::uint32_t flags)
        const;
};

using SignerListSet = SetSignerList;

}  // namespace ripple

#endif
