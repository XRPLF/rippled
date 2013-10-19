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

#ifndef BEAST_NET_BASICS_BUFFERTYPE_H_INCLUDED
#define BEAST_NET_BASICS_BUFFERTYPE_H_INCLUDED

#include "../mpl/IfCond.h"

namespace beast {

/** General linear memory buffer.
    This wraps the underlying buffer type and provides additional methods
    to create a uniform interface. Specializations allow asio-compatible
    buffers without having to include boost/asio.h.
*/
/** @{ */
template <bool IsConst>
class BufferType
{
private:
    typedef typename mpl::IfCond <IsConst,
        void const*,
        void*>::type pointer_type;

    typedef typename mpl::IfCond <IsConst,
        uint8 const,
        uint8>::type byte_type;

public:
    typedef std::size_t size_type;

    BufferType ()
        : m_data (nullptr)
        , m_size (0)
    {
    }

    template <bool OtherIsConst>
    BufferType (BufferType <OtherIsConst> const& other)
        : m_data (other.template cast <pointer_type> ())
        , m_size (other.size ())
    {
    }

    BufferType (pointer_type data, std::size_t size) noexcept
        : m_data (data)
        , m_size (size)
    {
    }

    BufferType& operator= (BufferType const& other) noexcept
    {
        m_data = other.cast <pointer_type> ();
        m_size = other.size ();
        return *this;
    }

    template <bool OtherIsConst>
    BufferType& operator= (
        BufferType <OtherIsConst> const& other) noexcept
    {
        m_data = other.template cast <pointer_type> ();
        m_size = other.size ();
        return *this;
    }

    template <typename T>
    T cast () const noexcept
    {
        return static_cast <T> (m_data);
    }

    size_type size () const
    {
        return m_size;
    }

    BufferType operator+ (size_type n) const noexcept
    {
        return BufferType (cast <byte_type*> (),
            size () - std::min (size(), n));
    }

private:
    pointer_type m_data;
    std::size_t m_size;
};
/** @} */

}

#endif
