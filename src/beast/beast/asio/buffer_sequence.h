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

#ifndef BEAST_ASIO_BUFFER_SEQUENCE_H_INCLUDED
#define BEAST_ASIO_BUFFER_SEQUENCE_H_INCLUDED

#include <boost/asio/buffer.hpp>

#include <beast/utility/noexcept.h>
#include <algorithm>
#include <iterator>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <vector>

namespace beast {
namespace asio {

template <class Buffer>
class buffer_sequence
{
private:
    typedef std::vector <Buffer> sequence_type;

public:
    typedef Buffer value_type;
    typedef typename sequence_type::const_iterator const_iterator;

private:
    sequence_type m_buffers;

    template <class FwdIter>
    void assign (FwdIter first, FwdIter last)
    {
        m_buffers.clear();
        m_buffers.reserve (std::distance (first, last));
        for (;first != last; ++first)
            m_buffers.push_back (*first);
    }

public:
    buffer_sequence ()
    {
    }

    template <
        class BufferSequence,
        class = std::enable_if_t <std::is_constructible <
            Buffer, typename BufferSequence::value_type>::value>
    >
    buffer_sequence (BufferSequence const& s)
    {
        assign (std::begin (s), std::end (s));
    }

    template <
        class FwdIter,
        class = std::enable_if_t <std::is_constructible <
            Buffer, typename std::iterator_traits <
            FwdIter>::value_type>::value>
    >
    buffer_sequence (FwdIter first, FwdIter last)
    {
        assign (first, last);
    }

    template <class BufferSequence>
    std::enable_if_t <std::is_constructible <
        Buffer, typename BufferSequence::value_type>::value,
        buffer_sequence&
    >
    operator= (BufferSequence const& s)
    {
        return assign (s);
    }

    const_iterator
    begin () const noexcept
    {
        return m_buffers.begin ();
    }

    const_iterator
    end () const noexcept
    {
        return m_buffers.end ();
    }

#if 0
    template <class ConstBufferSequence>
    void
    assign (ConstBufferSequence const& buffers)
    {
        auto const n (std::distance (
            std::begin (buffers), std::end (buffers)));

        for (int i = 0, auto iter (std::begin (buffers));
            iter != std::end (buffers); ++iter, ++i)
            m_buffers[i] = Buffer (boost::asio::buffer_cast <void*> (
                *iter), boost::asio::buffer_size (*iter));
    }
#endif
};

typedef buffer_sequence <boost::asio::const_buffer> const_buffers;
typedef buffer_sequence <boost::asio::mutable_buffer> mutable_buffers;

}
}

#endif
