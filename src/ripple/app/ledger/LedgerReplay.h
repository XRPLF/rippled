//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_LEDGER_LEDGERREPLAY_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERREPLAY_H_INCLUDED

#include <cstdint>
#include <map>
#include <memory>

namespace ripple {

class Ledger;
class STTx;

class LedgerReplay
{
    std::shared_ptr<Ledger const> parent_;
    std::shared_ptr<Ledger const> replay_;
    std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns_;

public:
    LedgerReplay(
        std::shared_ptr<Ledger const> parent,
        std::shared_ptr<Ledger const> replay);

    LedgerReplay(
        std::shared_ptr<Ledger const> parent,
        std::shared_ptr<Ledger const> replay,
        std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns);

    /** @return The parent of the ledger to replay
     */
    std::shared_ptr<Ledger const> const&
    parent() const
    {
        return parent_;
    }

    /** @return The ledger to replay
     */
    std::shared_ptr<Ledger const> const&
    replay() const
    {
        return replay_;
    }

    /** @return Transactions in the order they should be replayed
     */
    std::map<std::uint32_t, std::shared_ptr<STTx const>> const&
    orderedTxns() const
    {
        return orderedTxns_;
    }
};

}  // namespace ripple

#endif
