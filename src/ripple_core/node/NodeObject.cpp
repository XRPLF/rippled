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
    Blob& data,
    uint256 const& hash,
    PrivateAccess)
    : mType (type)
    , mHash (hash)
    , mLedgerIndex (ledgerIndex)
{
    // Take over the caller's buffer
    mData.swap (data);
}

NodeObject::Ptr NodeObject::createObject (
    NodeObjectType type,
    LedgerIndex ledgerIndex,
    Blob& data,
    uint256 const & hash)
{
    // The boost::ref is important or
    // else it will be passed by  value!
    return boost::make_shared <NodeObject> (
        type, ledgerIndex, boost::ref (data), hash, PrivateAccess ());
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

bool NodeObject::isCloneOf (NodeObject::Ptr const& other) const
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

class NodeObjectTests : public UnitTest
{
public:

    NodeObjectTests () : UnitTest ("NodeObject", "ripple")
    {
    }


    void runTest ()
    {
    }
};

static NodeObjectTests nodeObjectTests;

