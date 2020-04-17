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

#ifndef RIPPLE_NODESTORE_ENCODEDBLOB_H_INCLUDED
#define RIPPLE_NODESTORE_ENCODEDBLOB_H_INCLUDED

#include <ripple/basics/Buffer.h>
#include <ripple/nodestore/NodeObject.h>
#include <cstddef>

namespace ripple {
namespace NodeStore {

/** Utility for producing flattened node objects.
    @note This defines the database format of a NodeObject!
*/
// VFALCO TODO Make allocator aware and use short_alloc
struct EncodedBlob
{
public:
    void
    prepare(std::shared_ptr<NodeObject> const& object);

    void const*
    getKey() const noexcept
    {
        return m_key;
    }

    std::size_t
    getSize() const noexcept
    {
        return m_data.size();
    }

    void const*
    getData() const noexcept
    {
        return reinterpret_cast<void const*>(m_data.data());
    }

private:
    void const* m_key;
    Buffer m_data;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
