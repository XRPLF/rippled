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
                  uint32 ledgerIndex,
                  Blob const& binaryDataToCopy,
                  uint256 const& hash)
        : mType (type)
        , mHash (hash)
        , mLedgerIndex (ledgerIndex)
        , mData (binaryDataToCopy)
    {
    }

    /** Create from an area of memory.

        @note A copy of the data is created.
    */
	HashedObject (HashedObjectType type,
                  uint32 ledgerIndex,
                  void const* bufferToCopy,
                  int bytesInBuffer,
                  uint256 const& hash)
        : mType (type)
        , mHash (hash)
        , mLedgerIndex (ledgerIndex)
        , mData (static_cast <unsigned char const*> (bufferToCopy),
                 static_cast <unsigned char const*> (bufferToCopy) + bytesInBuffer)
    {
    }

    /** Retrieve the type of this object.
    */
    HashedObjectType getType () const
    {
        return mType;
    }

    /** Retrieve the hash metadata.
    */
    uint256 const& getHash() const
    {
        return mHash;
    }

    /** Retrieve the ledger index in which this object appears.
    */
    // VFALCO TODO rename to getLedgerIndex or getLedgerId
    uint32 getIndex () const
    {
        return mLedgerIndex;
    }

    /** Retrieve the binary data.
    */
	Blob const& getData() const
    {
        return mData;
    }
	
private:
	HashedObjectType const mType;
	uint256 const mHash;
	uint32 const mLedgerIndex;
	Blob const mData;
};

#endif
// vim:ts=4
