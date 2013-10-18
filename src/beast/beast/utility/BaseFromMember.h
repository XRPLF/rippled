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

#ifndef BEAST_UTILITY_BASEFROMMEMBER_H_INCLUDED
#define BEAST_UTILITY_BASEFROMMEMBER_H_INCLUDED

namespace beast {

template <typename T, int UniqueID = 0>
class BaseFromMember
{
private:
    T m_t;

public:
    BaseFromMember ()
        : m_t (T())
    {
    }

    template <class P1>
    explicit BaseFromMember (P1 const& p1)
        : m_t (p1)
        { }

    template <class P1, class P2>
    BaseFromMember (P1 const& p1, P2 const& p2)
        : m_t (p1, p2)
        { }

    template <class P1, class P2, class P3>
    BaseFromMember (P1 const& p1, P2 const& p2, P3 const& p3)
        : m_t (p1, p2, p3)
        { }

    template <class P1, class P2, class P3, class P4>
    BaseFromMember (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4)
        : m_t (p1, p2, p3, p4)
        { }

    template <class P1, class P2, class P3, class P4, class P5>
    BaseFromMember (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4, P5 const& p5)
        : m_t (p1, p2, p3, p4, p5)
        { }

    T& member()
        { return m_t; }

    T const& member() const
        { return m_t; }
};

}

#endif
