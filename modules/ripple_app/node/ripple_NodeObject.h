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

    /** Create from a key/value blob.

        This is the format in which a NodeObject is stored in the
        persistent storage layer.

        @see NodeStore
    */
    NodeObject (void const* key, void const* value, int valueBytes);

    /** Parsed key/value blob into NodeObject components.

        This will extract the information required to construct
        a NodeObject. It also does consistency checking and returns
        the result, so it is possible to determine if the data
        is corrupted without throwing an exception. Note all forms
        of corruption are detected so further analysis will be
        needed to eliminate false positives.

        This is the format in which a NodeObject is stored in the
        persistent storage layer.
    */
    struct DecodedBlob
    {
        DecodedBlob (void const* key, void const* value, int valueBytes);

        bool success;

        void const* key;
        LedgerIndex ledgerIndex;
        NodeObjectType objectType;
        unsigned char const* objectData;
        int dataBytes;
    };
        
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

private:
    NodeObjectType mType;
    uint256 mHash;
    LedgerIndex mLedgerIndex;
    Blob mData;
};

#endif
