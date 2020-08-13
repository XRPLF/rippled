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

#ifndef RIPPLE_APP_LEDGER_TRANSACTIONMASTER_H_INCLUDED
#define RIPPLE_APP_LEDGER_TRANSACTIONMASTER_H_INCLUDED

#include <ripple/app/misc/Transaction.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapTreeNode.h>

namespace ripple {

class Application;
class STTx;

// Tracks all transactions in memory

class TransactionMaster
{
public:
    TransactionMaster(Application& app);
    TransactionMaster(TransactionMaster const&) = delete;
    TransactionMaster&
    operator=(TransactionMaster const&) = delete;

    std::shared_ptr<Transaction>
    fetch_from_cache(uint256 const&);

    std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearchedAll>
    fetch(uint256 const&, error_code_i& ec);

    /**
     * Fetch transaction from the cache or database.
     *
     * @return A std::variant that contains either a
     *         shared_pointer to the retrieved transaction or a
     *         bool indicating whether or not the all ledgers in
     *         the provided range were present in the database
     *         while the search was conducted.
     */
    std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearchedAll>
    fetch(
        uint256 const&,
        ClosedInterval<uint32_t> const& range,
        error_code_i& ec);

    std::shared_ptr<STTx const>
    fetch(
        std::shared_ptr<SHAMapItem> const& item,
        SHAMapTreeNode::TNType type,
        std::uint32_t uCommitLedger);

    // return value: true = we had the transaction already
    bool
    inLedger(uint256 const& hash, std::uint32_t ledger);

    void
    canonicalize(std::shared_ptr<Transaction>* pTransaction);

    void
    sweep(void);

    TaggedCache<uint256, Transaction>&
    getCache();

private:
    Application& mApp;
    TaggedCache<uint256, Transaction> mCache;
};

}  // namespace ripple

#endif
