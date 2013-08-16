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

#ifndef BEAST_FIXEDINPUTBUFFER_H_INCLUDED
#define BEAST_FIXEDINPUTBUFFER_H_INCLUDED

/** Represents a small, fixed size buffer.
    This provides a convenient interface for doing a bytewise
    verification/reject test on a handshake protocol.
*/
template <int Bytes>
struct FixedInputBuffer
{
    template <typename ConstBufferSequence>
    explicit FixedInputBuffer (ConstBufferSequence const& buffer)
        : m_buffer (boost::asio::buffer (m_storage))
        , m_size (boost::asio::buffer_copy (m_buffer, buffer))
        , m_data (boost::asio::buffer_cast <uint8 const*> (m_buffer))
    {
    }

    uint8 operator[] (std::size_t index) const noexcept
    {
        bassert (index >= 0 && index < m_size);
        return m_data [index];
    }

    bool peek (std::size_t bytes) const noexcept
    {
        if (m_size >= bytes)
            return true;
        return false;
    }

    template <typename T>
    bool peek (T* t) noexcept
    {
        std::size_t const bytes = sizeof (T);
        if (m_size >= bytes)
        {
            std::copy (m_data, m_data + bytes, t);
            return true;
        }
        return false;
    }

    bool consume (std::size_t bytes) noexcept
    {
        if (m_size >= bytes)
        {
            m_data += bytes;
            m_size -= bytes;
            return true;
        }
        return false;
    }

    template <typename T>
    bool read (T* t) noexcept
    {
        std::size_t const bytes = sizeof (T);
        if (m_size >= bytes)
        {
            //this causes a stack corruption.
            //std::copy (m_data, m_data + bytes, t);

            memcpy (t, m_data, bytes);
            m_data += bytes;
            m_size -= bytes;
            return true;
        }
        return false;
    }

    // Reads an integraltype in network byte order
    template <typename IntegerType>
    bool readNetworkInteger (IntegerType* value)
    {
        // Must be an integral type!
        // not available in all versions of std:: unfortunately
        //static_bassert (std::is_integral <IntegerType>::value);
        IntegerType networkValue;
        if (! read (&networkValue))
            return;
        *value = fromNetworkByteOrder (networkValue);
        return true;
    }

private:
    boost::array <uint8, Bytes> m_storage;
    MutableBuffer m_buffer;
    std::size_t m_size;
    uint8 const* m_data;
};

#endif
