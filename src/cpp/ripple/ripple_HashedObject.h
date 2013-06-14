#ifndef RIPPLE_HASHEDOBJECT_H
#define RIPPLE_HASHEDOBJECT_H

/** The types of hashed objects.
*/
enum HashedObjectType
{
    hotUNKNOWN = 0,
    hotLEDGER = 1,
    hotTRANSACTION = 2,
    hotACCOUNT_NODE = 3,
    hotTRANSACTION_NODE = 4
};

DEFINE_INSTANCE (HashedObject);

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
class HashedObject : private IS_INSTANCE (HashedObject)
{
public:
    typedef boost::shared_ptr <HashedObject> pointer;

    /** Create from a vector of data.

        @note A copy of the data is created.
    */
    HashedObject (HashedObjectType type,
                  LedgerIndex ledgerIndex,
                  Blob const & binaryDataToCopy,
                  uint256 const & hash);

    /** Create from an area of memory.

        @note A copy of the data is created.
    */
    HashedObject (HashedObjectType type,
                  LedgerIndex ledgerIndex,
                  void const * bufferToCopy,
                  int bytesInBuffer,
                  uint256 const & hash);

    /** Retrieve the type of this object.
    */
    HashedObjectType getType () const;

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
    HashedObjectType const mType;
    uint256 const mHash;
    LedgerIndex const mLedgerIndex;
    Blob const mData;
};

#endif
// vim:ts=4
