//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

DecodedBlob::DecodedBlob (void const* key, void const* value, int valueBytes)
{
    /*  Data format:

        Bytes

        0...3       LedgerIndex     32-bit big endian integer
        4...7       Unused?         An unused copy of the LedgerIndex
        8           char            One of NodeObjectType
        9...end                     The body of the object data
    */

    m_success = false;
    m_key = key;
    // VFALCO NOTE Ledger indexes should have started at 1
    m_ledgerIndex = LedgerIndex (-1);
    m_objectType = hotUNKNOWN;
    m_objectData = nullptr;
    m_dataBytes = bmax (0, valueBytes - 9);

    if (valueBytes > 4)
    {
        LedgerIndex const* index = static_cast <LedgerIndex const*> (value);
        m_ledgerIndex = ByteOrder::swapIfLittleEndian (*index);
    }

    // VFALCO NOTE What about bytes 4 through 7 inclusive?

    if (valueBytes > 8)
    {
        unsigned char const* byte = static_cast <unsigned char const*> (value);
        m_objectType = static_cast <NodeObjectType> (byte [8]);
    }

    if (valueBytes > 9)
    {
        m_objectData = static_cast <unsigned char const*> (value) + 9;

        switch (m_objectType)
        {
        case hotUNKNOWN:
        default:
            break;

        case hotLEDGER:
        case hotTRANSACTION:
        case hotACCOUNT_NODE:
        case hotTRANSACTION_NODE:
            m_success = true;
            break;
        }
    }
}

NodeObject::Ptr DecodedBlob::createObject ()
{
    bassert (m_success);

    NodeObject::Ptr object;

    if (m_success)
    {
        Blob data (m_dataBytes);

        memcpy (data.data (), m_objectData, m_dataBytes);

        object = NodeObject::createObject (
            m_objectType, m_ledgerIndex, data, uint256::fromVoid (m_key));
    }

    return object;
}

}
