//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef RIPPLE_SYNC_OBJECTREQUESTER_H_INCLUDED
#define RIPPLE_SYNC_OBJECTREQUESTER_H_INCLUDED

#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/sync/CopyLedger.h>

#include <cstddef>
#include <utility>

namespace ripple {

class SHAMapInnerNode;

namespace sync {

/**
 * An `ObjectRequester` is tightly coupled with a `CopyLedger`.
 * It builds requests for objects (identified by their digests),
 * queuing them after they reach a count limit.
 *
 * The public API is limited:
 *
 * - `request`: Call with a digest the first time it is requested. This is
 *   mostly called by another public API method, `deserialize`. The only time
 *   it is called by `CopyLedger` is for the root object, the ledger header.
 * - `rerequest`: Call with a digest every time it is requested after the
 *   first. This is called by `CopyLedger` when it fails to find a requested
 *   object in a response (whether it is missing or invalid).
 * - `deserialize`: Call with an object the first time it is found. This is
 *   called by `CopyLedger` when it is reading responses, but also by
 *   `request` and `rerequest` when an object is found in the local database.
 *
 * Each request can hold up to `MAX_OBJECTS_PER_MESSAGE` digests.
 * When it is constructed, `ObjectRequester` peels back the last queued
 * request from `CopyLedger` if it still has room for more digests.
 * Once a request becomes full,
 * `ObjectRequester` will queue it and start another.
 * When it is destroyed,
 * `ObjectRequester` queues any partial request it was last building, if any.
 *
 * `request` and `rerequest` are both thin layers over `request_`.
 * `request_` will first look in the local object database,
 * and if it cannot find the object there,
 * will add its digest to the next request.
 *
 * `ObjectRequester` is not thread-safe.
 */
class ObjectRequester
{
private:
    using RequestPtr = CopyLedger::RequestPtr;

    CopyLedger& copier_;
    // REVIEWER: Is it safe to cache a lookup of the full below cache
    // generation?
    RequestPtr request_;

    /**
     * An object is requested if its digest ever appears in a request.
     * An object is received if it is ever found after being requested.
     * An object that is requested (because it was not found in the local
     * database), but not delivered in a response, and when re-requested is
     * found in the local database (because of some other workflow),
     * is still received.
     * We account in this way because we call the finish in `CopyLedger` when
     * received equals requested.
     */
    std::size_t received_ = 0;
    std::size_t requested_ = 0;

    /** The number of attempts to find an object. */
    std::size_t searched_ = 0;
    /** The number of objects found in the database. */
    std::size_t loaded_ = 0;
    // searched = loaded + requested + rerequested

public:
    ObjectRequester(CopyLedger&);
    ~ObjectRequester();

    /** Request an object for the first time. */
    void
    request(ObjectDigest const& digest)
    {
        return _request(digest, 0);
    }

    /** Request an object for a subsequent time. */
    void
    rerequest(ObjectDigest const& digest)
    {
        return _request(digest, 1);
    }

    void
    deserialize(ObjectDigest const& digest, Slice const& slice);

private:
    /**
     * @param requested The number of times this object has been requested
     * before.
     */
    void
    _request(ObjectDigest const&, std::size_t requested);

    void
    _send();
};

}  // namespace sync
}  // namespace ripple

#endif
