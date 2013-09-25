//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_ENCODEDBLOB_H_INCLUDED
#define RIPPLE_NODESTORE_ENCODEDBLOB_H_INCLUDED

namespace NodeStore
{

/** Utility for producing flattened node objects.

    These get recycled to prevent many small allocations.

    @note This defines the database format of a NodeObject!
*/
struct EncodedBlob
{
public:
    typedef RecycledObjectPool <EncodedBlob> Pool;

    void prepare (NodeObject::Ptr const& object);

    void const* getKey () const noexcept { return m_key; }

    size_t getSize () const noexcept { return m_size; }

    void const* getData () const noexcept { return m_data.getData (); }

private:
    void const* m_key;
    MemoryBlock m_data;
    size_t m_size;
};

}

#endif
