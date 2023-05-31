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

#ifndef RIPPLE_PEERCLIENT_PEERCLIENT_H_INCLUDED
#define RIPPLE_PEERCLIENT_PEERCLIENT_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/promises.h>
#include <ripple/ledger/LedgerHeader.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/peerclient/MessageScheduler.h>
// TODO: Move `TxSet` into a separate header?
#include <ripple/peerclient/TxSetRequester.h>
// TODO: Move `SHAMapKey` into a separate header?
#include <ripple/peerclient/ProofRequester.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/shamap/SHAMapLeafNode.h>

#include <vector>

namespace ripple {

// REVIEW: Move `SkipList` to its own header?
/** Skip lists are ordered oldest to newest. */
using SkipList = std::vector<LedgerDigest>;

class PeerClient
{
private:
    Scheduler& jscheduler_;
    // TODO: Try to get rid of direct dependency on `Application`.
    Application& app_;

public:
    PeerClient(Application& app, Scheduler& jscheduler);

    // TODO: Accept multiple digests.
    FuturePtr<std::shared_ptr<protocol::TMGetObjectByHash>>
    getObject(
        protocol::TMGetObjectByHash::ObjectType type,
        ObjectDigest&& digest);

    FuturePtr<LedgerHeader>
    getHeader(LedgerDigest&& digest);

    FuturePtr<TxSet>
    getTxSet(LedgerDigest&& digest);

    FuturePtr<std::shared_ptr<SHAMapLeafNode>>
    getLeaf(LedgerDigest&& ledgerDigest, SHAMapKey&& key);

    FuturePtr<SkipList>
    getSkipList(LedgerDigest&& digest);
};

}  // namespace ripple

#endif
