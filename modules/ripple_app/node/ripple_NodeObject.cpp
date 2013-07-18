//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (NodeObject)

//------------------------------------------------------------------------------

NodeObject::NodeObject (
    NodeObjectType type,
    LedgerIndex ledgerIndex,
    Blob const& binaryDataToCopy,
    uint256 const& hash)
    : mType (type)
    , mHash (hash)
    , mLedgerIndex (ledgerIndex)
    , mData (binaryDataToCopy)
{
}

NodeObject::NodeObject (
    NodeObjectType type,
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

NodeObjectType NodeObject::getType () const
{
    return mType;
}

uint256 const& NodeObject::getHash () const
{
    return mHash;
}

LedgerIndex NodeObject::getIndex () const
{
    return mLedgerIndex;
}

Blob const& NodeObject::getData () const
{
    return mData;
}

bool NodeObject::isCloneOf (NodeObject const& other) const
{
    return
        mType == other.mType &&
        mHash == other.mHash &&
        mLedgerIndex == other.mLedgerIndex &&
        mData == other.mData
        ;
}

//------------------------------------------------------------------------------

class NodeObjectTests : public UnitTest
{
public:

    NodeObjectTests () : UnitTest ("NodeObject")
    {
    }


    void runTest ()
    {
    }
};

static NodeObjectTests nodeObjectTests;

