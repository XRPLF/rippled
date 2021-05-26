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

#ifndef RIPPLE_NODESTORE_BACKEND_H_INCLUDED
#define RIPPLE_NODESTORE_BACKEND_H_INCLUDED

#include <ripple/nodestore/Types.h>
#include <atomic>
#include <cstdint>

namespace ripple {
namespace NodeStore {

/** A backend used for the NodeStore.

    The NodeStore uses a swappable backend so that other database systems
    can be tried. Different databases may offer various features such
    as improved performance, fault tolerant or distributed storage, or
    all in-memory operation.

    A given instance of a backend is fixed to a particular key size.
*/
class Backend
{
public:
    template <typename T>
    struct Counters
    {
        Counters() = default;
        Counters(Counters const&) = default;

        template <typename U>
        Counters(Counters<U> const& other)
            : writeDurationUs(other.writeDurationUs)
            , writeRetries(other.writeRetries)
            , writesDelayed(other.writesDelayed)
            , readRetries(other.readRetries)
            , readErrors(other.readErrors)
        {
        }

        T writeDurationUs = {};
        T writeRetries = {};
        T writesDelayed = {};
        T readRetries = {};
        T readErrors = {};
    };

    /** Destroy the backend.

        All open files are closed and flushed. If there are batched writes
        or other tasks scheduled, they will be completed before this call
        returns.
    */
    virtual ~Backend() = default;

    /** Get the human-readable name of this backend.
        This is used for diagnostic output.
    */
    virtual std::string
    getName() = 0;

    /** Open the backend.
        @param createIfMissing Create the database files if necessary.
        This allows the caller to catch exceptions.
    */
    virtual void
    open(bool createIfMissing = true) = 0;

    /** Returns true is the database is open.
     */
    virtual bool
    isOpen() = 0;

    /** Open the backend.
        @param createIfMissing Create the database files if necessary.
        @param appType Deterministic appType used to create a backend.
        @param uid Deterministic uid used to create a backend.
        @param salt Deterministic salt used to create a backend.
        @throws std::runtime_error is function is called not for NuDB backend.
    */
    virtual void
    open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt)
    {
        Throw<std::runtime_error>(
            "Deterministic appType/uid/salt not supported by backend " +
            getName());
    }

    /** Close the backend.
        This allows the caller to catch exceptions.
    */
    virtual void
    close() = 0;

    /** Fetch a single object.
        If the object is not found or an error is encountered, the
        result will indicate the condition.
        @note This will be called concurrently.
        @param key A pointer to the key data.
        @param pObject [out] The created object if successful.
        @return The result of the operation.
    */
    virtual Status
    fetch(void const* key, std::shared_ptr<NodeObject>* pObject) = 0;

    /** Fetch a batch synchronously. */
    virtual std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256 const*> const& hashes) = 0;

    /** Store a single object.
        Depending on the implementation this may happen immediately
        or deferred using a scheduled task.
        @note This will be called concurrently.
        @param object The object to store.
    */
    virtual void
    store(std::shared_ptr<NodeObject> const& object) = 0;

    /** Store a group of objects.
        @note This function will not be called concurrently with
              itself or @ref store.
    */
    virtual void
    storeBatch(Batch const& batch) = 0;

    virtual void
    sync() = 0;

    /** Visit every object in the database
        This is usually called during import.
        @note This routine will not be called concurrently with itself
              or other methods.
        @see import
    */
    virtual void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) = 0;

    /** Estimate the number of write operations pending. */
    virtual int
    getWriteLoad() = 0;

    /** Remove contents on disk upon destruction. */
    virtual void
    setDeletePath() = 0;

    /** Perform consistency checks on database.
     *
     * This method is implemented only by NuDBBackend. It is not yet called
     * anywhere, but it might be a good idea to one day call it at startup to
     * avert a crash.
     */
    virtual void
    verify()
    {
    }

    /** Returns the number of file descriptors the backend expects to need. */
    virtual int
    fdRequired() const = 0;

    /** Returns read and write stats.

        @note The Counters struct is specific to and only used
              by CassandraBackend.
    */
    virtual std::optional<Counters<std::uint64_t>>
    counters() const
    {
        return std::nullopt;
    }
};

}  // namespace NodeStore
}  // namespace ripple

#endif
