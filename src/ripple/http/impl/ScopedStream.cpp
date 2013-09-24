//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

ScopedStream::ScopedStream (Session& session)
    : m_session (session)
{
}

ScopedStream::ScopedStream (ScopedStream const& other)
    : m_session (other.m_session)
{
}

ScopedStream::ScopedStream (Session& session,
                            std::ostream& manip (std::ostream&))
    : m_session (session)
{
    m_ostream << manip;
}

ScopedStream::~ScopedStream ()
{
    if (! m_ostream.str().empty())
        m_session.write (m_ostream.str());
}

std::ostream& ScopedStream::operator<< (std::ostream& manip (std::ostream&)) const
{
    return m_ostream << manip;
}

std::ostringstream& ScopedStream::ostream () const
{
    return m_ostream;
}

}
}
