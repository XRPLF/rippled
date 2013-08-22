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

#ifndef BEAST_BUFFERTYPE_H_INCLUDED
#define BEAST_BUFFERTYPE_H_INCLUDED

/** Storage for a BufferSequence.

    Meets these requirements:
        BufferSequence
        ConstBufferSequence (when Buffer is mutable_buffer)
        MutableBufferSequence (when Buffer is const_buffer)
*/
template <class Buffer>
class BufferType
{
public:
    typedef Buffer value_type;
    typedef typename std::vector <Buffer>::const_iterator const_iterator;

    /** Construct a null buffer.
        This is the equivalent of @ref asio::null_buffers.
    */
    BufferType ()
        : m_size (0)
    {
    }

    /** Construct a BufferType from an existing BufferSequence.
        @see assign
    */
    template <class BufferSequence>
    BufferType (BufferSequence const& buffers)
    {
        assign (buffers);
    }

    /** Assign a BufferType from an existing BufferSequence.
        @see assign
    */
    template <class BufferSequence>
    BufferType <Buffer>& operator= (BufferSequence const& buffers)
    {
        return assign (buffers);
    }

    /** Assign a BufferType from an existing BufferSequence
        A copy is not made. The data is still owned by the original
        BufferSequence object. This merely points to that data.
    */
    template <class BufferSequence>
    BufferType <Buffer>& assign (BufferSequence const& buffers)
    {
        m_size = 0;
        m_buffers.clear ();
        m_buffers.reserve (std::distance (buffers.begin (), buffers.end ()));
        BOOST_FOREACH (typename BufferSequence::value_type buffer, buffers)
        {
            m_size += boost::asio::buffer_size (buffer);
            m_buffers.push_back (buffer);
        }
        return *this;
    }

    /** Determine the total size of all buffers.
        This is faster than calling boost::asio::buffer_size.
    */
    std::size_t size () const noexcept
    {
        return m_size;
    }

    const_iterator begin () const noexcept
    {
        return m_buffers.begin ();
    }

    const_iterator end () const noexcept
    {
        return m_buffers.end ();
    }

    /** Retrieve a consumed BufferSequence. */
    BufferType consumed (std::size_t bytes) const
    {
        BufferType result;
        result.m_buffers.reserve (m_buffers.size ());
        BOOST_FOREACH (Buffer buffer, m_buffers)
        {
            std::size_t const have = boost::asio::buffer_size (buffer);
            std::size_t const reduce = std::min (bytes, have);
            bytes -= reduce;

            if (have > reduce)
                result.m_buffers.push_back (buffer + reduce);
        }
        return result;
    }

private:
    std::size_t m_size;
    std::vector <Buffer> m_buffers;
};

/** A single linear read-only buffer. */
typedef boost::asio::const_buffer ConstBuffer;

/** A single linear writable buffer. */
typedef boost::asio::mutable_buffer MutableBuffer;

/** Meets the requirements of ConstBufferSequence */
typedef BufferType <ConstBuffer> ConstBuffers;

/** Meets the requirements of MutableBufferSequence */
typedef BufferType <MutableBuffer> MutableBuffers;

#endif
