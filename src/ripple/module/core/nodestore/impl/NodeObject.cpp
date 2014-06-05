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

namespace ripple {

SETUP_LOG (NodeObject)

//------------------------------------------------------------------------------

NodeObject::NodeObject (
    NodeObjectType type,
    LedgerIndex ledgerIndex,
    Blob&& data,
    uint256 const& hash,
    PrivateAccess)
    : mType (type)
    , mHash (hash)
    , mLedgerIndex (ledgerIndex)
{
    mData = std::move (data);
}

NodeObject::Ptr NodeObject::createObject (
    NodeObjectType type,
    LedgerIndex ledgerIndex,
    Blob&& data,
    uint256 const & hash)
{
    return std::make_shared <NodeObject> (
        type, ledgerIndex, std::move (data), hash, PrivateAccess ());
}

NodeObjectType
NodeObject::getType () const
{
    return mType;
}

uint256 const&
NodeObject::getHash () const
{
    return mHash;
}

LedgerIndex
NodeObject::getLedgerIndex () const
{
    return mLedgerIndex;
}

Blob const&
NodeObject::getData () const
{
    return mData;
}

bool 
NodeObject::isCloneOf (NodeObject::Ptr const& other) const
{
    if (mType != other->mType)
        return false;

    if (mHash != other->mHash)
        return false;

    if (mLedgerIndex != other->mLedgerIndex)
        return false;

    if (mData != other->mData)
        return false;

    return true;
}

//------------------------------------------------------------------------------

}
