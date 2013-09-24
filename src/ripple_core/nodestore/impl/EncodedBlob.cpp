//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

void EncodedBlob::prepare (NodeObject::Ptr const& object)
{
    m_key = object->getHash ().begin ();

    // This is how many bytes we need in the flat data
    m_size = object->getData ().size () + 9;

    m_data.ensureSize (m_size);

    // These sizes must be the same!
    static_bassert (sizeof (uint32) == sizeof (object->getIndex ()));

    {
        uint32* buf = static_cast <uint32*> (m_data.getData ());

        buf [0] = ByteOrder::swapIfLittleEndian (object->getIndex ());
        buf [1] = ByteOrder::swapIfLittleEndian (object->getIndex ());
    }

    {
        unsigned char* buf = static_cast <unsigned char*> (m_data.getData ());

        buf [8] = static_cast <unsigned char> (object->getType ());

        memcpy (&buf [9], object->getData ().data (), object->getData ().size ());
    }
}

}
