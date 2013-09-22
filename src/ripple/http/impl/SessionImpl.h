//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_SESSIONIMPL_H_INCLUDED
#define RIPPLE_HTTP_SESSIONIMPL_H_INCLUDED

namespace ripple {
namespace HTTP {

using namespace beast;

// Holds the copy of buffers being sent
typedef SharedArg <std::string> SharedBuffer;

class Peer;

class SessionImpl : public Session
{
public:
    Peer& m_peer;
    bool m_closed;
    boost::optional <boost::asio::io_service::work> m_work;

    explicit SessionImpl (Peer& peer);
    ~SessionImpl ();
    bool closed() const;
    void write (void const* buffer, std::size_t bytes);
    void close();
    void detach();
};

}
}

#endif
