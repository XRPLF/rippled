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

#if BEAST_INCLUDE_BEASTCONFIG
#include "../../BeastConfig.h"
#endif

#include <beast/net/DynamicBuffer.h>

#include <algorithm>
#include <memory>

namespace beast {

DynamicBuffer::DynamicBuffer (size_type blocksize)
    : m_blocksize (blocksize)
    , m_size (0)
{
}

DynamicBuffer::~DynamicBuffer ()
{
    for (Buffers::iterator iter (m_buffers.begin());
        iter != m_buffers.end(); ++iter)
        free (*iter);
}

void DynamicBuffer::swapWith (DynamicBuffer& other)
{
    std::swap (m_blocksize, other.m_blocksize);
    std::swap (m_size, other.m_size);
    m_buffers.swap (other.m_buffers);
}

void DynamicBuffer::commit (size_type n)
{
    m_size += n;
    bassert (m_size <= m_buffers.size () * m_blocksize);
}

DynamicBuffer::size_type DynamicBuffer::size () const
{
    return m_size;
}

void DynamicBuffer::reserve (size_type n)
{
    size_type count ((m_size + n + m_blocksize - 1) / m_blocksize);
    if (count > m_buffers.size ())
        for (count -= m_buffers.size (); count-- > 0;)
            m_buffers.push_back (malloc (m_blocksize));
}

void DynamicBuffer::shrink_to_fit ()
{
    size_type const count ((m_size + m_blocksize - 1) / m_blocksize);
    while (m_buffers.size () > count)
    {
        free (m_buffers.back ());
        m_buffers.erase (m_buffers.end () - 1);
    }   
}

std::string DynamicBuffer::to_string () const
{
    std::string (s);
    s.reserve (m_size);
    std::size_t amount (m_size);
    for (Buffers::const_iterator iter (m_buffers.begin());
        amount > 0 && iter != m_buffers.end(); ++iter)
    {
        char const* p (static_cast <char const*> (*iter));
        size_type const n (std::min (amount, m_blocksize));
        s.append (p, p + n);
        amount -= n;
    }
    return s;
}

}
