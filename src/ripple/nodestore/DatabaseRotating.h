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

#ifndef RIPPLE_NODESTORE_DATABASEROTATING_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASEROTATING_H_INCLUDED

#include <ripple/nodestore/Database.h>

namespace ripple {
namespace NodeStore {

/* This class has two key-value store Backend objects for persisting SHAMap
 * records. This facilitates online deletion of data. New backends are
 * rotated in. Old ones are rotated out and deleted.
 */

class DatabaseRotating
{
public:
    virtual ~DatabaseRotating() = default;

    virtual TaggedCache <uint256, NodeObject>& getPositiveCache() = 0;

    virtual std::mutex& peekMutex() const = 0;

    virtual std::shared_ptr <Backend> const& getWritableBackend() const = 0;

    virtual std::shared_ptr <Backend> const& getArchiveBackend () const = 0;

    virtual std::shared_ptr <Backend> rotateBackends (
            std::shared_ptr <Backend> const& newBackend) = 0;

    /** Ensure that node is in writableBackend */
    virtual std::shared_ptr<NodeObject> fetchNode (uint256 const& hash) = 0;
};

}
}

#endif
