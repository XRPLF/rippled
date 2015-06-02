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

#include <BeastConfig.h>
#include <ripple/nodestore/impl/DatabaseRotatingImp.h>

namespace ripple {
namespace NodeStore {

// Make sure to call it already locked!
std::shared_ptr <Backend> DatabaseRotatingImp::rotateBackends (
        std::shared_ptr <Backend> const& newBackend)
{
    std::shared_ptr <Backend> oldBackend = archiveBackend_;
    archiveBackend_ = writableBackend_;
    writableBackend_ = newBackend;

    return oldBackend;
}

std::shared_ptr<NodeObject> DatabaseRotatingImp::fetchFrom (uint256 const& hash)
{
    Backends b = getBackends();
    std::shared_ptr<NodeObject> object = fetchInternal (*b.writableBackend, hash);
    if (!object)
    {
        object = fetchInternal (*b.archiveBackend, hash);
        if (object)
        {
            getWritableBackend()->store (object);
            m_negCache.erase (hash);
        }
    }

    return object;
}
}

}
