//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (NodeObject)

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

NodeObject::NodeObject (void const* key, void const* value, int valueBytes)
{
    DecodedBlob decoded (key, value, valueBytes);

    if (decoded.success)
    {
        mType = decoded.objectType;
        mHash = uint256 (key);
        mLedgerIndex = decoded.ledgerIndex;
        mData = Blob (decoded.objectData, decoded.objectData + decoded.dataBytes);
    }
    else
    {
        // VFALCO TODO Write the hex version of key to the string for diagnostics.
        String s;
        s << "NodeStore:: DecodedBlob failed";
        Throw (s);
    }
}

NodeObject::DecodedBlob::DecodedBlob (void const* key, void const* value, int valueBytes)
{
    /*  Data format:

        Bytes

        0...3       LedgerIndex     32-bit big endian integer
        4...7       Unused?         An unused copy of the LedgerIndex
        8           char            One of NodeObjectType
        9...end                     The body of the object data
    */

    success = false;
    key = key;
    // VFALCO NOTE Ledger indexes should have started at 1
    ledgerIndex = LedgerIndex (-1);
    objectType = hotUNKNOWN;
    objectData = nullptr;
    dataBytes = bmin (0, valueBytes - 9);

    if (dataBytes > 4)
    {
        LedgerIndex const* index = static_cast <LedgerIndex const*> (value);
        ledgerIndex = ByteOrder::swapIfLittleEndian (*index);
    }

    // VFALCO NOTE What about bytes 4 through 7 inclusive?

    if (dataBytes > 8)
    {
        unsigned char const* byte = static_cast <unsigned char const*> (value);
        objectType = static_cast <NodeObjectType> (byte [8]);
    }

    if (dataBytes > 9)
    {
        objectData = static_cast <unsigned char const*> (value) + 9;

        switch (objectType)
        {
        case hotUNKNOWN:
        default:
            break;

        case hotLEDGER:
        case hotTRANSACTION:
        case hotACCOUNT_NODE:
        case hotTRANSACTION_NODE:
            success = true;
            break;
        }
    }
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
