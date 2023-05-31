//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/peerclient/PeerClient.h>

#include <ripple/basics/Coroutine.h>
#include <ripple/basics/utility.h>
#include <ripple/peerclient/ObjectsRequester.h>
#include <ripple/peerclient/ProofRequester.h>
#include <ripple/peerclient/TxSetRequester.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/messages.h>

#include <stdexcept>

namespace ripple {

PeerClient::PeerClient(Application& app, Scheduler& jscheduler)
    : jscheduler_(jscheduler), app_(app)
{
}

FuturePtr<std::shared_ptr<protocol::TMGetObjectByHash>>
PeerClient::getObject(
    protocol::TMGetObjectByHash::ObjectType type,
    ObjectDigest&& digest)
{
    return start<ObjectsRequester>(app_, jscheduler_, type, std::move(digest));
}

FuturePtr<LedgerHeader>
PeerClient::getHeader(LedgerDigest&& digest)
{
    return getObject(protocol::TMGetObjectByHash::otLEDGER, std::move(digest))
        ->thenv([](auto const& response) {
            auto const& object = response->objects(0);
            auto slice = makeSlice(object.data());
            // TODO: Does `PeerImp` verify the hash? I doubt it...
            auto header = deserializePrefixedHeader(slice, /*hasHash=*/false);
            header.hash = object.hash();
            return header;
        });
}

FuturePtr<TxSet>
PeerClient::getTxSet(LedgerDigest&& digest)
{
    return start<TxSetRequester>(app_, jscheduler_, std::move(digest));
}

FuturePtr<std::shared_ptr<SHAMapLeafNode>>
PeerClient::getLeaf(LedgerDigest&& ledgerDigest, SHAMapKey&& key)
{
    return start<ProofRequester>(
        app_, jscheduler_, std::move(ledgerDigest), std::move(key));
}

FuturePtr<SkipList>
PeerClient::getSkipList(LedgerDigest&& digest)
{
    return getLeaf(std::move(digest), ripple::copy(keylet::skip().key))
        ->thenv([](auto const& leaf) {
            auto const& item = leaf->peekItem();
            assert(item);
            auto sle =
                std::make_shared<SLE>(SerialIter{item->slice()}, item->key());
            assert(sle);
            return sle->getFieldV256(sfHashes).value();
        });
}

}  // namespace ripple
