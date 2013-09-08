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

ContentBodyBuffer::ContentBodyBuffer (size_type blocksize)
    : m_blocksize (blocksize)
    , m_size (0)
{
}

ContentBodyBuffer::~ContentBodyBuffer ()
{
    for (Handles::iterator iter (m_handles.begin());
        iter != m_handles.end(); ++iter)
    {
        void* const buffer (*iter);
        std::free (buffer);
    }
}

void ContentBodyBuffer::swapWith (ContentBodyBuffer& other)
{
    std::swap (m_blocksize, other.m_blocksize);
    std::swap (m_size, other.m_size);
    m_handles.swap (other.m_handles);
}

void ContentBodyBuffer::commit (size_type n)
{
    m_size += n;
    bassert (m_size <= m_handles.size () * m_blocksize);
}

ConstBuffers ContentBodyBuffer::data () const
{
    size_type n (m_size);
    std::vector <ConstBuffer> v;
    v.reserve ((m_size + m_blocksize - 1) / m_blocksize);
    for (Handles::const_iterator iter (m_handles.begin());
        iter != m_handles.end() && n > 0; ++iter)
    {
        size_type const amount (std::min (n, m_blocksize));
        v.push_back (MutableBuffer (*iter, amount));
        n -= amount;
    }
    return ConstBuffers (v);
}

ContentBodyBuffer::size_type ContentBodyBuffer::size () const
{
    return m_size;
}

MutableBuffers ContentBodyBuffer::prepare (size_type n)
{
    reserve (n);
    std::vector <MutableBuffer> v;
    size_type offset (m_size % m_blocksize);
    for (Handles::iterator iter = m_handles.begin () + (m_size / m_blocksize);
        iter != m_handles.end () && n > 0; ++iter)
    {
        size_type const amount (std::min (n, m_blocksize - offset));
        v.push_back (MutableBuffer (*iter, amount));
        n -= amount;
        offset = 0;
    }
    return MutableBuffers (v);
}

void ContentBodyBuffer::reserve (size_type n)
{
    size_type count ((m_size + n + m_blocksize - 1) / m_blocksize);
    if (count > m_handles.size ())
        for (count -= m_handles.size (); count-- > 0;)
            m_handles.push_back (std::malloc (m_blocksize));
}

void ContentBodyBuffer::shrink_to_fit ()
{
    size_type const count ((m_size + m_blocksize - 1) / m_blocksize);
    while (m_handles.size () > count)
    {
        std::free (m_handles.back ());
        m_handles.erase (m_handles.end () - 1);
    }   
}