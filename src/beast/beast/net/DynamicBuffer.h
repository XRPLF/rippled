//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_NET_DYNAMICBUFFER_H_INCLUDED
#define BEAST_NET_DYNAMICBUFFER_H_INCLUDED

#include <string>
#include <vector>

namespace beast {

/** Disjoint, but efficient buffer storage for network operations.
    This is designed to not require asio in order to compile.
*/
class DynamicBuffer
{
public:
    enum
    {
        defaultBlocksize = 32 * 1024
    };

    typedef std::size_t             size_type;

    /** Create the dynamic buffer with the specified block size. */
    explicit DynamicBuffer (size_type blocksize = defaultBlocksize);
    
    DynamicBuffer (DynamicBuffer const& other);

    ~DynamicBuffer ();

    DynamicBuffer& operator= (DynamicBuffer const& other);

    /** Swap the contents of this buffer with another.
        This is the preferred way to transfer ownership.
    */
    void swapWith (DynamicBuffer& other);

    /** Returns the size of the input sequence. */
    size_type size () const;

    /** Returns a buffer to the input sequence.
        ConstBufferType must be constructible with this signature:
            ConstBufferType (void const* buffer, size_type bytes);
    */
    template <typename ConstBufferType>
    std::vector <ConstBufferType>
    data () const
    {
        std::vector <ConstBufferType> buffers;
        buffers.reserve (m_buffers.size());
        size_type amount (m_size);
        for (typename Buffers::const_iterator iter (m_buffers.begin());
            amount > 0 && iter != m_buffers.end(); ++iter)
        {
            size_type const n (std::min (amount, m_blocksize));
            buffers.push_back (ConstBufferType (*iter, n));
            amount -= n;
        }
        return buffers;
    }

    /** Reserve space in the output sequence.
        This also returns a buffer suitable for writing.
        MutableBufferType must be constructible with this signature:
            MutableBufferType (void* buffer, size_type bytes);
    */
    template <typename MutableBufferType>
    std::vector <MutableBufferType>
    prepare (size_type amount)
    {
        std::vector <MutableBufferType> buffers;
        buffers.reserve (m_buffers.size());
        reserve (amount);
        size_type offset (m_size % m_blocksize);
        for (Buffers::iterator iter = m_buffers.begin () + (m_size / m_blocksize);
            amount > 0 && iter != m_buffers.end (); ++iter)
        {
            size_type const n (std::min (amount, m_blocksize - offset));
            buffers.push_back (MutableBufferType (
                ((static_cast <char*> (*iter)) + offset), n));
            amount -= n;
            offset = 0;
        }
        return buffers;
    }

    /** Reserve space in the output sequence. */
    void reserve (size_type n);

    /** Move bytes from the output to the input sequence. */
    void commit (size_type n);

    /** Release memory while preserving the input sequence. */
    void shrink_to_fit ();

    /** Convert the entire buffer into a single string.
        This is mostly for diagnostics, it defeats the purpose of the class!
    */
    std::string to_string () const;

private:
    typedef std::vector <void*> Buffers;

    size_type m_blocksize;
    size_type m_size;
    Buffers m_buffers;
};

}

#endif
