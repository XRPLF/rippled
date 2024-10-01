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

#include <xrpld/nodestore/detail/DecodedBlob.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <algorithm>

namespace ripple {
namespace NodeStore {

DecodedBlob::DecodedBlob(void const* key, void const* value, int valueBytes)
{
    /*  Data format:

        Bytes

        0...7       Unused
        8           char            One of NodeObjectType
        9...end                     The body of the object data
    */

    m_success = false;
    m_key = key;
    m_objectType = hotUNKNOWN;
    m_objectData = nullptr;
    m_dataBytes = std::max(0, valueBytes - 9);

    // VFALCO NOTE What about bytes 4 through 7 inclusive?

    if (valueBytes > 8)
    {
        unsigned char const* byte = static_cast<unsigned char const*>(value);
        m_objectType = safe_cast<NodeObjectType>(byte[8]);
    }

    if (valueBytes > 9)
    {
        m_objectData = static_cast<unsigned char const*>(value) + 9;

        switch (m_objectType)
        {
            default:
                break;

            case hotUNKNOWN:
            case hotLEDGER:
            case hotACCOUNT_NODE:
            case hotTRANSACTION_NODE:
                m_success = true;
                break;
        }
    }
}

std::shared_ptr<NodeObject>
DecodedBlob::createObject()
{
    ASSERT(
        m_success,
        "ripple::NodeStore::DecodedBlob::createObject : valid object type");

    std::shared_ptr<NodeObject> object;

    if (m_success)
    {
        Blob data(m_objectData, m_objectData + m_dataBytes);

        object = NodeObject::createObject(
            m_objectType, std::move(data), uint256::fromVoid(m_key));
    }

    return object;
}

}  // namespace NodeStore
}  // namespace ripple
