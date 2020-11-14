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

#ifndef RIPPLE_APP_LEDGER_TRANSACTIONSTATESF_H_INCLUDED
#define RIPPLE_APP_LEDGER_TRANSACTIONSTATESF_H_INCLUDED

#include <ripple/app/ledger/AbstractFetchPackContainer.h>
#include <ripple/nodestore/Database.h>
#include <ripple/shamap/SHAMapSyncFilter.h>

namespace ripple {

// This class is only needed on add functions
// sync filter for transactions tree during ledger sync
class TransactionStateSF : public SHAMapSyncFilter
{
public:
    TransactionStateSF(NodeStore::Database& db, AbstractFetchPackContainer& fp)
        : db_(db), fp_(fp)
    {
    }

    void
    gotNode(
        bool fromFilter,
        SHAMapHash const& nodeHash,
        std::uint32_t ledgerSeq,
        Blob&& nodeData,
        SHAMapNodeType type) const override;

    boost::optional<Blob>
    getNode(SHAMapHash const& nodeHash) const override;

private:
    NodeStore::Database& db_;
    AbstractFetchPackContainer& fp_;
};

}  // namespace ripple

#endif
