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

#include <ripple/nodestore/impl/DatabaseNodeImp.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

void
DatabaseNodeImp::store(NodeObjectType type, Blob&& data,
    uint256 const& hash, std::uint32_t seq)
{
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
    assert(hash == sha512Hash(makeSlice(data)));
#endif
    auto nObj = NodeObject::createObject(type, std::move(data), hash);
    pCache_->canonicalize(hash, nObj, true);
    backend_->store(nObj);
    nCache_->erase(hash);
    storeStats(nObj->getData().size());
}

bool
DatabaseNodeImp::asyncFetch(uint256 const& hash,
    std::uint32_t seq, std::shared_ptr<NodeObject>& object)
{
    // See if the object is in cache
    object = pCache_->fetch(hash);
    if (object || nCache_->touch_if_exists(hash))
        return true;
    // Otherwise post a read
    Database::asyncFetch(hash, seq, pCache_, nCache_);
    return false;
}

bool
DatabaseNodeImp::copyLedger(
    std::shared_ptr<Ledger const> const& ledger)
{
    if (ledger->info().hash.isZero() ||
        ledger->info().accountHash.isZero())
    {
        assert(false);
        JLOG(j_.error()) <<
            "Invalid ledger";
        return false;
    }
    auto& srcDB = const_cast<Database&>(
        ledger->stateMap().family().db());
    if (&srcDB == this)
    {
        assert(false);
        JLOG(j_.error()) <<
            "Source and destination are the same";
        return false;
    }
    Batch batch;
    bool error = false;
    auto f = [&](SHAMapAbstractNode& node) {
        if (auto nObj = srcDB.fetch(
            node.getNodeHash().as_uint256(), ledger->info().seq))
                batch.emplace_back(std::move(nObj));
        else
            error = true;
        return !error;
    };
    // Batch the ledger header
    {
        Serializer s(1024);
        s.add32(HashPrefix::ledgerMaster);
        addRaw(ledger->info(), s);
        batch.emplace_back(NodeObject::createObject(hotLEDGER,
            std::move(s.modData()), ledger->info().hash));
    }
    // Batch the state map
    if (ledger->stateMap().getHash().isNonZero())
    {
        if (! ledger->stateMap().isValid())
        {
            JLOG(j_.error()) <<
                "invalid state map";
            return false;
        }
        ledger->stateMap().snapShot(false)->visitNodes(f);
        if (error)
            return false;
    }
    // Batch the transaction map
    if (ledger->info().txHash.isNonZero())
    {
        if (! ledger->txMap().isValid())
        {
            JLOG(j_.error()) <<
                "invalid transaction map";
            return false;
        }
        ledger->txMap().snapShot(false)->visitNodes(f);
        if (error)
            return false;
    }
    // Store batch
    for (auto& nObj : batch)
    {
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
        assert(nObj->getHash() == sha512Hash(makeSlice(nObj->getData())));
#endif
        pCache_->canonicalize(nObj->getHash(), nObj, true);
        nCache_->erase(nObj->getHash());
        storeStats(nObj->getData().size());
    }
    backend_->storeBatch(batch);
    return true;
}

void
DatabaseNodeImp::tune(int size, int age)
{
    pCache_->setTargetSize(size);
    pCache_->setTargetAge(age);
    nCache_->setTargetSize(size);
    nCache_->setTargetAge(age);
}

void
DatabaseNodeImp::sweep()
{
    pCache_->sweep();
    nCache_->sweep();
}

} // NodeStore
} // ripple
