//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_TYPES_AGEDHISTORY_H_INCLUDED
#define RIPPLE_TYPES_AGEDHISTORY_H_INCLUDED

namespace ripple {

// Simple container swapping template
template <class Container>
class AgedHistory
{
public:
    typedef Container container_type;

    AgedHistory()
        : m_p1 (&m_c1)
        , m_p2 (&m_c2)
    {
    }

    AgedHistory (AgedHistory const& other)
        : m_c1 (other.front())
        , m_c2 (other.back())
        , m_p1 (&m_c1)
        , m_p2 (&m_c2)
    {
    }

    AgedHistory& operator= (AgedHistory const& other)
    {
        m_c1 = other.front();
        m_c2 = other.back();
        m_p1 = &m_c1;
        m_p2 = &m_c2;
        return *this;
    }

    void swap () { std::swap (m_p1, m_p2); }

    Container*       operator-> ()       { return m_p1; }
    Container const* operator-> () const { return m_p1; }

    Container&       front()       { return *m_p1; }
    Container const& front() const { return *m_p1; }
    Container&       back()        { return *m_p2; }
    Container const& back()  const { return *m_p2; }

private:
    Container  m_c1;
    Container  m_c2;
    Container* m_p1;
    Container* m_p2;
};

}

#endif
