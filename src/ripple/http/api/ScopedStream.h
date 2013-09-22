//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_SCOPEDSTREAM_H_INCLUDED
#define RIPPLE_HTTP_SCOPEDSTREAM_H_INCLUDED

#include <ostream>

namespace ripple {
namespace HTTP {

using namespace beast;

class Session;

/** Scoped ostream-based RAII container for building the HTTP response. */
class ScopedStream
{
public:
    explicit ScopedStream (Session& session);
    ScopedStream (ScopedStream const& other);

    template <typename T>
    ScopedStream (Session& session, T const& t)
        : m_session (session)
    {
        m_ostream << t;
    }

    ScopedStream (Session& session, std::ostream& manip (std::ostream&));

    ~ScopedStream ();

    std::ostringstream& ostream () const;

    std::ostream& operator<< (std::ostream& manip (std::ostream&)) const;

    template <typename T>
    std::ostream& operator<< (T const& t) const
    {
        return m_ostream << t;
    }

private:
    ScopedStream& operator= (ScopedStream const&); // disallowed

    Session& m_session;
    std::ostringstream mutable m_ostream;
};

}
}

#endif
