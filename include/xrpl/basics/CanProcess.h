//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_CANPROCESS_H_INCLUDED
#define RIPPLE_BASICS_CANPROCESS_H_INCLUDED

#include <functional>
#include <mutex>
#include <set>

/** RAII class to check if an Item is already being processed on another thread,
 * as indicated by it's presence in a Collection.
 *
 * If the Item is not in the Collection, it will be added under lock in the
 * ctor, and removed under lock in the dtor. The object will be considered
 * "usable" and evaluate to `true`.
 *
 * If the Item is in the Collection, no changes will be made to the collection,
 * and the CanProcess object will be considered "unusable".
 *
 * It's up to the caller to decide what "usable" and "unusable" mean. (e.g.
 * Process or skip a block of code, or set a flag.)
 *
 * The current use is to avoid lock contention that would be involved in
 * processing something associated with the Item.
 *
 * Examples:
 *
 * void IncomingLedgers::acquireAsync(LedgerHash const& hash, ...)
 * {
 *    if (CanProcess check{acquiresMutex_, pendingAcquires_, hash})
 *    {
 *      acquire(hash, ...);
 *    }
 * }
 *
 * bool
 * NetworkOPsImp::recvValidation(
 *   std::shared_ptr<STValidation> const& val,
 *   std::string const& source)
 *   {
 *       CanProcess check(
 *           validationsMutex_, pendingValidations_, val->getLedgerHash());
 *       BypassAccept bypassAccept =
 *           check ? BypassAccept::no : BypassAccept::yes;
 *       handleNewValidation(app_, val, source, bypassAccept, m_journal);
 *   }
 *
 */
class CanProcess
{
public:
    template <class Mutex, class Collection, class Item>
    CanProcess(Mutex& mtx, Collection& collection, Item const& item)
        : cleanup_(insert(mtx, collection, item))
    {
    }

    ~CanProcess()
    {
        if (cleanup_)
            cleanup_();
    }

    explicit
    operator bool() const
    {
        return static_cast<bool>(cleanup_);
    }

private:
    template <bool useIterator, class Mutex, class Collection, class Item>
    std::function<void()>
    doInsert(Mutex& mtx, Collection& collection, Item const& item)
    {
        std::unique_lock<Mutex> lock(mtx);
        auto const [it, inserted] = collection.insert(item);
        if (!inserted)
            return {};
        if constexpr (useIterator)
            return [&, it]() {
                std::unique_lock<Mutex> lock(mtx);
                collection.erase(it);
            };
        else
            return [&]() {
                std::unique_lock<Mutex> lock(mtx);
                collection.erase(item);
            };
    }

    // Generic insert() function doesn't use iterators because they may get
    // invalidated
    template <class Mutex, class Collection, class Item>
    std::function<void()>
    insert(Mutex& mtx, Collection& collection, Item const& item)
    {
        return doInsert<false>(mtx, collection, item);
    }

    // Specialize insert() for std::set, which does not invalidate iterators for
    // insert and erase
    template <class Mutex, class Item>
    std::function<void()>
    insert(Mutex& mtx, std::set<Item>& collection, Item const& item)
    {
        return doInsert<true>(mtx, collection, item);
    }

    // If set, then the item is "usable"
    std::function<void()> cleanup_;
};

#endif
