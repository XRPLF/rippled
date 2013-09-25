//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_PORT_H_INCLUDED
#define RIPPLE_HTTP_PORT_H_INCLUDED

namespace ripple {
using namespace beast;

namespace HTTP {

/** Configuration information for a server listening port. */
struct Port
{
    enum Security
    {
        no_ssl,
        allow_ssl,
        require_ssl
    };

    Port ();
    Port (Port const& other);
    Port& operator= (Port const& other);
    Port (uint16 port_, IPEndpoint const& addr_,
            Security security_, SSLContext* context_);

    uint16 port;
    IPEndpoint addr;
    Security security;
    SSLContext* context;
};

int  compare    (Port const& lhs, Port const& rhs);
bool operator== (Port const& lhs, Port const& rhs);
bool operator!= (Port const& lhs, Port const& rhs);
bool operator<  (Port const& lhs, Port const& rhs);
bool operator<= (Port const& lhs, Port const& rhs);
bool operator>  (Port const& lhs, Port const& rhs);
bool operator>= (Port const& lhs, Port const& rhs);

/** A set of listening ports settings. */
typedef std::vector <Port> Ports;

}
}

#endif
