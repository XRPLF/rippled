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

#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/main/Application.h>
#include <ripple/protocol/STTx.h>
#include <ripple/basics/chrono.h>

namespace ripple {

TransactionMaster::TransactionMaster (Application& app)
    : mApp (app)
    , mCache ("TransactionCache", 65536, std::chrono::minutes {30}, stopwatch(),
        mApp.journal("TaggedCache"))
{
}

bool TransactionMaster::inLedger (uint256 const& hash, std::uint32_t ledger)
{
    auto txn = mCache.fetch (hash);

    if (!txn)
        return false;

    txn->setStatus (COMMITTED, ledger);
    return true;
}

std::shared_ptr<Transaction>
TransactionMaster::fetch_from_cache (uint256 const& txnID)
{
    return mCache.fetch (txnID);
}

std::shared_ptr<Transaction>
TransactionMaster::fetch (uint256 const& txnID, error_code_i& ec)
{
    auto txn = fetch_from_cache (txnID);

    if (txn)
        return txn;

    txn = Transaction::load (txnID, mApp, ec);

    if (!txn)
        return txn;

    mCache.canonicalize (txnID, txn);

    return txn;
}

Transaction::variant
TransactionMaster::fetch (uint256 const& txnID, error_code_i& ec,
    ClosedInterval<uint32_t> const& range)
{
    auto txn = mCache.fetch (txnID);

    if (txn)
        return txn;

    auto v = Transaction::load (txnID, mApp, ec, range);

    if (v.index () == 0 && std::get<0> (v))
        mCache.canonicalize (txnID, std::get<0> (v));

    return v;
}

std::shared_ptr<STTx const>
TransactionMaster::fetch (std::shared_ptr<SHAMapItem> const& item,
    SHAMapTreeNode::TNType type, std::uint32_t uCommitLedger)
{
    std::shared_ptr<STTx const>  txn;
    auto iTx = fetch_from_cache (item->key());

    if (!iTx)
    {

        if (type == SHAMapTreeNode::tnTRANSACTION_NM)
        {
            SerialIter sit (item->slice());
            txn = std::make_shared<STTx const> (std::ref (sit));
        }
        else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
        {
            auto blob = SerialIter{item->data(), item->size()}.getVL();
            txn = std::make_shared<STTx const>(SerialIter{blob.data(), blob.size()});
        }
    }
    else
    {
        if (uCommitLedger)
            iTx->setStatus (COMMITTED, uCommitLedger);

        txn = iTx->getSTransaction ();
    }

    return txn;
}

void
TransactionMaster::canonicalize(std::shared_ptr<Transaction>* pTransaction)
{
    uint256 const tid = (*pTransaction)->getID();
    if (tid != beast::zero)
    {
        auto txn = *pTransaction;
        // VFALCO NOTE canonicalize can change the value of txn!
        mCache.canonicalize(tid, txn);
        *pTransaction = txn;
    }
}

void TransactionMaster::sweep (void)
{
    mCache.sweep ();
}

TaggedCache <uint256, Transaction>& TransactionMaster::getCache()
{
    return mCache;
}

} // ripple
