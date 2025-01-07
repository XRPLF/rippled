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

#include <mutex>
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
 *           check.canProcess() ? BypassAccept::no : BypassAccept::yes;
 *       handleNewValidation(app_, val, source, bypassAccept, m_journal);
 *   }
 *
 */
template <class Mutex, class Collection, class Item>
class CanProcess
{
public:
    CanProcess(Mutex& mtx, Collection& collection, Item const& item)
        : mtx_(mtx), collection_(collection), item_(item), canProcess_(insert())
    {
    }

    ~CanProcess()
    {
        if (canProcess_)
        {
            std::unique_lock<Mutex> lock_(mtx_);
            collection_.erase(item_);
        }
    }

    bool
    canProcess() const
    {
        return canProcess_;
    }

    operator bool() const
    {
        return canProcess_;
    }

private:
    bool
    insert()
    {
        std::unique_lock<Mutex> lock_(mtx_);
        auto const [_, inserted] = collection_.insert(item_);
        return inserted;
    }

    Mutex& mtx_;
    Collection& collection_;
    Item const item_;
    bool const canProcess_;
};

#endif
