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

#ifndef RIPPLE_NODESTORE_DATABASE_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASE_H_INCLUDED

#include <ripple/nodestore/NodeObject.h>

namespace ripple {
namespace NodeStore {

/** Persistency layer for NodeObject

    A Node is a ledger object which is uniquely identified by a key, which is
    the 256-bit hash of the body of the node. The payload is a variable length
    block of serialized data.

    All ledger data is stored as node objects and as such, needs to be persisted
    between launches. Furthermore, since the set of node objects will in
    general be larger than the amount of available memory, purged node objects
    which are later accessed must be retrieved from the node store.

    @see NodeObject
*/
class Database
{
public:
    /** Destroy the node store.
        All pending operations are completed, pending writes flushed,
        and files closed before this returns.
    */
    virtual ~Database () = 0 ;

    /** Retrieve the name associated with this backend.
        This is used for diagnostics and may not reflect the actual path
        or paths used by the underlying backend.
    */
    virtual beast::String getName () const = 0;

    /** Fetch an object.
        If the object is known to be not in the database, isn't found in the
        database during the fetch, or failed to load correctly during the fetch,
        `nullptr` is returned.

        @note This can be called concurrently.
        @param hash The key of the object to retrieve.  
        @return The object, or nullptr if it couldn't be retrieved.
    */
    virtual NodeObject::pointer fetch (uint256 const& hash) = 0;

    /** Fetch an object without waiting.
        If I/O is required to determine whether or not the object is present,
        `false` is returned. Otherwise, `true` is returned and `object` is set
        to refer to the object, or `nullptr` if the object is not present.
        If I/O is required, the I/O is scheduled.

        @note This can be called concurrently.
        @param hash The key of the object to retrieve
        @param object The object retrieved
        @return Whether the operation completed
    */
    virtual bool asyncFetch (uint256 const& hash, NodeObject::pointer& object) = 0;

    /** Wait for all currently pending async reads to complete.
    */
    virtual void waitReads () = 0;

    /** Get the maximum number of async reads the node store prefers.
        @return The number of async reads preferred.
    */
    virtual int getDesiredAsyncReadCount () = 0;

    /** Store the object.

        The caller's Blob parameter is overwritten.

        @param type The type of object.
        @param ledgerIndex The ledger in which the object appears.
        @param data The payload of the object. The caller's
                    variable is overwritten.
        @param hash The 256-bit hash of the payload data.

        @return `true` if the object was stored?              
    */
    virtual void store (NodeObjectType type,
                        std::uint32_t ledgerIndex,
                        Blob&& data,
                        uint256 const& hash) = 0;

    /** Visit every object in the database
        This is usually called during import.

        @note This routine will not be called concurrently with itself
                or other methods.
        @see import
    */
    virtual void for_each(std::function <void(NodeObject::Ptr)> f) = 0;

    /** Import objects from another database. */
    virtual void import (Database& source) = 0;

    /** Retrieve the estimated number of pending write operations.
        This is used for diagnostics.
    */
    virtual int getWriteLoad () = 0;

    /** Get the positive cache hits to total attempts ratio. */
    virtual float getCacheHitRate () = 0;

    /** Set the maximum number of entries and maximum cache age for both caches.
        
        @param size Number of cache entries (0 = ignore)
        @param age Maximum cache age in seconds
    */
    virtual void tune (int size, int age) = 0;

    /** Remove expired entries from the positive and negative caches. */
    virtual void sweep () = 0;
};

}
}

#endif
