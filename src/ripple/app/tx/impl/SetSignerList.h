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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/basics/Log.h>
#include <algorithm>
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
    enum Operation {unknown, set, destroy};
    Operation do_ {unknown};
    std::uint32_t quorum_ {0};
    std::vector<SignerEntries::SignerEntry> signers_;

    // Deserialize a signer list and return true if successful.
    // The list will be sorted by account, but needs to be checked to ensure
    // there are no duplicates or incorrect entries.
    static bool extract(STArray const& entries,
        std::vector<std::pair<AccountID, std::uint16_t>>& result);

public:
    explicit SetSignerList (ApplyContext& ctx)
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

    TER doApply () override;
    void preCompute() override;

private:
    static
    std::tuple<NotTEC, std::uint32_t,
        std::vector<SignerEntries::SignerEntry>,
            Operation>
    determineOperation(STTx const& tx,
        ApplyFlags flags, beast::Journal j);

    static
    NotTEC validateQuorumAndSignerEntries (
        std::uint32_t quorum,
            std::vector<SignerEntries::SignerEntry> const& signers,
                AccountID const& account,
                    beast::Journal j);

    TER replaceSignerList ();
    TER destroySignerList ();

    TER removeSignersFromLedger (Keylet const& accountKeylet,
        Keylet const& ownerDirKeylet, Keylet const& signerListKeylet);
    void writeSignersToSLE (
        SLE::pointer const& ledgerEntry, std::uint32_t flags) const;

    // This is how we computed the owner count prior to activation of the
    // featureMultiSignReserve amendment. This needs to stay in the code
    // until no SignerLists created prior to that amendment are left in the
    // ledger, which means it'll be here effectively forever.
    static int legacyOwnerCountDelta (std::size_t entryCount)
    {
        assert (entryCount >= STTx::minMultiSigners);
        assert (entryCount <= STTx::maxMultiSigners);

        return 2 + static_cast<int>(entryCount);
    }
};

} // ripple

#endif
