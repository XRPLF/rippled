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
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <beast/ByteOrder.h>

namespace ripple {
namespace NodeStore {

void
EncodedBlob::prepare (std::shared_ptr<NodeObject> const& object)
{
    m_key = object->getHash().begin ();

    // This is how many bytes we need in the flat data
    m_size = object->getData ().size () + 9;

    m_data.ensureSize(m_size);

    {
        // these 8 bytes are unused
        std::uint64_t* buf = static_cast <
            std::uint64_t*>(m_data.getData ());
        *buf = 0;
    }

    {
        unsigned char* buf = static_cast <
            unsigned char*> (m_data.getData ());
        buf [8] = static_cast <
            unsigned char> (object->getType ());
        memcpy (&buf [9], object->getData ().data(),
            object->getData ().size());
    }
}

}
}
