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

#ifndef RIPPLE_NODESTORE_NODEOBJECT_H_INCLUDED
#define RIPPLE_NODESTORE_NODEOBJECT_H_INCLUDED

#include <ripple/basics/utility/CountedObject.h>
#include <ripple/module/data/protocol/Protocol.h>

// VFALCO NOTE Intentionally not in the NodeStore namespace

namespace ripple {

/** The types of node objects. */
enum NodeObjectType
{
    hotUNKNOWN = 0,
    hotLEDGER = 1,
    hotTRANSACTION = 2,
    hotACCOUNT_NODE = 3,
    hotTRANSACTION_NODE = 4
};

/** A simple object that the Ledger uses to store entries. 
    NodeObjects are comprised of a type, a hash, a ledger index and a blob. 
    They can be uniquely identified by the hash, which is a SHA 256 of the 
    blob. The blob is a variable length block of serialized data. The type 
    identifies what the blob contains.

    @note No checking is performed to make sure the hash matches the data.
    @see SHAMap
*/
class NodeObject : public CountedObject <NodeObject>
{
public:
    static char const* getCountedObjectName () { return "NodeObject"; }

    enum
    {
        /** Size of the fixed keys, in bytes.

            We use a 256-bit hash for the keys.

            @see NodeObject
        */
        keyBytes = 32,
    };

    // Please use this one. For a reference use Ptr const&
    typedef std::shared_ptr <NodeObject> Ptr;

    // These are DEPRECATED, type names are capitalized.
    typedef std::shared_ptr <NodeObject> pointer;
    typedef pointer const& ref;

private:
    // This hack is used to make the constructor effectively private
    // except for when we use it in the call to make_shared.
    // There's no portable way to make make_shared<> a friend work.
    struct PrivateAccess { };
public:
    // This constructor is private, use createObject instead.
    NodeObject (NodeObjectType type,
                LedgerIndex ledgerIndex,
                Blob&& data,
                uint256 const& hash,
                PrivateAccess);

    /** Create an object from fields.

        The caller's variable is modified during this call. The
        underlying storage for the Blob is taken over by the NodeObject.

        @param type The type of object.
        @param ledgerIndex The ledger in which this object appears.
        @param data A buffer containing the payload. The caller's variable
                    is overwritten.
        @param hash The 256-bit hash of the payload data.
    */
    static Ptr createObject (NodeObjectType type,
                             LedgerIndex ledgerIndex,
                             Blob&& data,
                             uint256 const& hash);

    /** Retrieve the type of this object.
    */
    NodeObjectType getType () const;

    /** Retrieve the hash metadata.
    */
    uint256 const& getHash () const;

    /** Retrieve the ledger index in which this object appears.
    */
    LedgerIndex getLedgerIndex() const;

    /** Retrieve the binary data.
    */
    Blob const& getData () const;

    /** See if this object has the same data as another object.
    */
    bool isCloneOf (NodeObject::Ptr const& other) const;

    /** Binary function that satisfies the strict-weak-ordering requirement.

        This compares the hashes of both objects and returns true if
        the first hash is considered to go before the second.

        @see std::sort
    */
    struct LessThan
    {
        inline bool operator() (NodeObject::Ptr const& lhs, NodeObject::Ptr const& rhs) const noexcept
        {
            return lhs->getHash () < rhs->getHash ();
        }
    };

private:
    NodeObjectType mType;
    uint256 mHash;
    LedgerIndex mLedgerIndex;
    Blob mData;
};

}

#endif
