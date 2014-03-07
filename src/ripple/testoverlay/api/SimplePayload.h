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

#ifndef RIPPLE_TESTOVERLAY_SIMPLEPAYLOAD_H_INCLUDED
#define RIPPLE_TESTOVERLAY_SIMPLEPAYLOAD_H_INCLUDED

namespace TestOverlay
{

/** A simple message payload. */
class SimplePayload
{
public:
    SimplePayload ()
    {
    }

    SimplePayload (int what, beast::String data = beast::String::empty, int hops = 0)
        : m_hops (hops)
        , m_what (what)
        , m_data (data)
    {
    }

    SimplePayload (SimplePayload const& other)
        : m_hops (other.m_hops)
        , m_what (other.m_what)
        , m_data (other.m_data)
    {
    }

    SimplePayload& operator= (SimplePayload const& other)
    {
        m_hops = other.m_hops;
        m_what = other.m_what;
        m_data = other.m_data;
        return *this;
    }

    SimplePayload withHop () const
    {
        return SimplePayload (m_what, m_data, m_hops + 1);
    }

    int hops () const
    {
        return m_hops;
    }

    int what () const
    {
        return m_what;
    }

    beast::String data () const
    {
        return m_data;
    }

private:
    int m_hops;
    int m_what;
    beast::String m_data;
};

}

#endif
