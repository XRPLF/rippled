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

#ifndef RIPPLE_NODESTORE_DECODEDBLOB_H_INCLUDED
#define RIPPLE_NODESTORE_DECODEDBLOB_H_INCLUDED

#include <ripple/nodestore/NodeObject.h>

namespace ripple {
namespace NodeStore {

/** Parsed key/value blob into NodeObject components.

    This will extract the information required to construct a NodeObject. It
    also does consistency checking and returns the result, so it is possible
    to determine if the data is corrupted without throwing an exception. Not
    all forms of corruption are detected so further analysis will be needed
    to eliminate false negatives.

    @note This defines the database format of a NodeObject!
*/
class DecodedBlob
{
public:
    /** Construct the decoded blob from raw data. */
    DecodedBlob (void const* key, void const* value, int valueBytes);

    /** Determine if the decoding was successful. */
    bool wasOk () const noexcept { return m_success; }

    /** Create a NodeObject from this data. */
    std::shared_ptr<NodeObject> createObject ();

private:
    bool m_success;

    void const* m_key;
    NodeObjectType m_objectType;
    unsigned char const* m_objectData;
    int m_dataBytes;
};

}
}

#endif
