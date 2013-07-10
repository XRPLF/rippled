//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HASHEDOBJECT_H
#define RIPPLE_HASHEDOBJECT_H

/** The types of hashed objects.
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
// VFALCO TODO consider making the instance a private member of SHAMap
//         since its the primary user.
//
class NodeObject
    : public CountedObject <NodeObject>
{
public:
    static char const* getCountedObjectName () { return "NodeObject"; }

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

private:
    NodeObjectType const mType;
    uint256 const mHash;
    LedgerIndex const mLedgerIndex;
    Blob const mData;
};

#endif
// vim:ts=4
