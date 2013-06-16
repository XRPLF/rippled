//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (HashedObject)

DECLARE_INSTANCE (HashedObject);

HashedObject::HashedObject (
    HashedObjectType type,
    LedgerIndex ledgerIndex,
    Blob const& binaryDataToCopy,
    uint256 const& hash)
    : mType (type)
    , mHash (hash)
    , mLedgerIndex (ledgerIndex)
    , mData (binaryDataToCopy)
{
}

HashedObject::HashedObject (
    HashedObjectType type,
    LedgerIndex ledgerIndex,
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

HashedObjectType HashedObject::getType () const
{
    return mType;
}

uint256 const& HashedObject::getHash () const
{
    return mHash;
}

LedgerIndex HashedObject::getIndex () const
{
    return mLedgerIndex;
}

Blob const& HashedObject::getData () const
{
    return mData;
}
