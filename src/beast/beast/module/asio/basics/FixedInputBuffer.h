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

#ifndef BEAST_ASIO_BASICS_FIXEDINPUTBUFFER_H_INCLUDED
#define BEAST_ASIO_BASICS_FIXEDINPUTBUFFER_H_INCLUDED

#include <beast/asio/buffer_sequence.h>
#include <array>

namespace beast {
namespace asio {

/** Represents a small, fixed size buffer.
    This provides a convenient interface for doing a bytewise
    verification/reject test on a handshake protocol.
*/
/** @{ */
class FixedInputBuffer
{
protected:
    struct CtorParams
    {
        CtorParams (std::uint8_t const* begin_, std::size_t bytes_)
            : begin (begin_)
            , bytes (bytes_)
        {
        }

        std::uint8_t const* begin;
        std::size_t bytes;
    };

    FixedInputBuffer (CtorParams const& params)
        : m_begin (params.begin)
        , m_iter (m_begin)
        , m_end (m_begin + params.bytes)
    {
    }

public:
    FixedInputBuffer (FixedInputBuffer const& other)
        : m_begin (other.m_begin)
        , m_iter (other.m_iter)
        , m_end (other.m_end)
    {
    }

    FixedInputBuffer& operator= (FixedInputBuffer const& other)
    {
        m_begin = other.m_begin;
        m_iter = other.m_iter;
        m_end = other.m_end;
        return *this;
    }

    // Returns the number of bytes consumed
    std::size_t used () const noexcept
    {
        return m_iter - m_begin;
    }

    // Returns the size of what's remaining
    std::size_t size () const noexcept
    {
        return m_end - m_iter;
    }

    void const* peek (std::size_t bytes)
    {
        return peek_impl (bytes, nullptr);
    }

    template <typename T>
    bool peek (T* t) const noexcept
    {
        return peek_impl (sizeof (T), t) != nullptr;
    }

    bool consume (std::size_t bytes) noexcept
    {
        return read_impl (bytes, nullptr) != nullptr;
    }

    bool read (std::size_t bytes) noexcept
    {
        return read_impl (bytes, nullptr) != nullptr;
    }

    template <typename T>
    bool read (T* t) noexcept
    {
        return read_impl (sizeof (T), t) != nullptr;
    }

    std::uint8_t operator[] (std::size_t index) const noexcept
    {
        bassert (index >= 0 && index < size ());
        return m_iter [index];
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
            return false;
        *value = fromNetworkByteOrder (networkValue);
        return true;
    }

protected:
    void const* peek_impl (std::size_t bytes, void* buffer) const noexcept
    {
        if (size () >= bytes)
        {
            if (buffer != nullptr)
                memcpy (buffer, m_iter, bytes);
            return m_iter;
        }
        return nullptr;
    }

    void const* read_impl (std::size_t bytes, void* buffer) noexcept
    {
        if (size () >= bytes)
        {
            if (buffer != nullptr)
                memcpy (buffer, m_iter, bytes);
            void const* data = m_iter;
            m_iter += bytes;
            return data;
        }
        return nullptr;
    }

private:
    std::uint8_t const* m_begin;
    std::uint8_t const* m_iter;
    std::uint8_t const* m_end;
};

//------------------------------------------------------------------------------

template <int Bytes>
class FixedInputBufferSize : public FixedInputBuffer
{
protected:
    struct SizedCtorParams
    {
        template <typename ConstBufferSequence, typename Storage>
        SizedCtorParams (ConstBufferSequence const& buffers, Storage& storage)
        {
            boost::asio::mutable_buffer buffer (boost::asio::buffer (storage));
            data = boost::asio::buffer_cast <std::uint8_t const*> (buffer);
            bytes = boost::asio::buffer_copy (buffer, buffers);
        }

        operator CtorParams () const noexcept
        {
            return CtorParams (data, bytes);
        }

        std::uint8_t const* data;
        std::size_t bytes;
    };

public:
    template <typename ConstBufferSequence>
    explicit FixedInputBufferSize (ConstBufferSequence const& buffers)
        : FixedInputBuffer (SizedCtorParams (buffers, m_storage))
    {
    }

private:
    std::array <std::uint8_t, Bytes> m_storage;
    boost::asio::mutable_buffer m_buffer;
};

}
}

#endif
