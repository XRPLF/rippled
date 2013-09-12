//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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

    SimplePayload (int what, String data = String::empty, int hops = 0)
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

    String data () const
    {
        return m_data;
    }

private:
    int m_hops;
    int m_what;
    String m_data;
};

}

#endif
