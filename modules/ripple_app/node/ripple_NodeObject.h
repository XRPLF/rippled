//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODEOBJECT_H_INCLUDED
#define RIPPLE_NODEOBJECT_H_INCLUDED

/** The types of node objects.
*/
enum NodeObjectType
{
    hotUNKNOWN = 0,
    hotLEDGER = 1,
    hotTRANSACTION = 2,
    hotACCOUNT_NODE = 3,
    hotTRANSACTION_NODE = 4
};

/** A blob of data with associated metadata, referenced by hash.

    The metadata includes the following:

    - Type of the blob
    - The ledger index in which it appears
    - The SHA 256 hash

    @note No checking is performed to make sure the hash matches the data.
    @see SHAMap
*/
class NodeObject : public CountedObject <NodeObject>
{
public:
    static char const* getCountedObjectName () { return "NodeObject"; }

    /** The type used to hold the hash.

        The hahes are fixed size, SHA256.

        @note The key size can be retrieved with `Hash::sizeInBytes`
    */
    typedef UnsignedInteger <32> Hash;

    typedef boost::shared_ptr <NodeObject> pointer;
    typedef pointer const& ref;

    /** Create from a vector of data.

        @note A copy of the data is created.
    */
    NodeObject (NodeObjectType type,
                LedgerIndex ledgerIndex,
                Blob const & binaryDataToCopy,
                uint256 const & hash);

    /** Create from an area of memory.

        @note A copy of the data is created.
    */
    NodeObject (NodeObjectType type,
                LedgerIndex ledgerIndex,
                void const * bufferToCopy,
                int bytesInBuffer,
                uint256 const & hash);

    /** Retrieve the type of this object.
    */
    NodeObjectType getType () const;

    /** Retrieve the hash metadata.
    */
    uint256 const& getHash () const;

    /** Retrieve the ledger index in which this object appears.
    */
    // VFALCO TODO rename to getLedgerIndex or getLedgerId
    LedgerIndex getIndex () const;

    /** Retrieve the binary data.
    */
    Blob const& getData () const;

    /** See if this object has the same data as another object.
    */
    bool isCloneOf (NodeObject const& other) const;

private:
    NodeObjectType mType;
    uint256 mHash;
    LedgerIndex mLedgerIndex;
    Blob mData;
};

#endif
