//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

Port::Port ()
    : port (0)
    , security (no_ssl)
    , context (nullptr)
{
}

Port::Port (Port const& other)
    : port (other.port)
    , addr (other.addr)
    , security (other.security)
    , context (other.context)
{
}

Port& Port::operator= (Port const& other)
{
    port = other.port;
    addr = other.addr;
    security = other.security;
    context = other.context;
    return *this;
}

Port::Port (
    uint16 port_,
    IPEndpoint const& addr_,
    Security security_,
    SSLContext* context_)
    : port (port_)
    , addr (addr_)
    , security (security_)
    , context (context_)
{
}

int compare (Port const& lhs, Port const& rhs)
{
    int comp;
    
    comp = compare (lhs.addr, rhs.addr);
    if (comp != 0)
        return comp;

    if (lhs.port < rhs.port)
        return -1;
    else if (lhs.port > rhs.port)
        return 1;

    if (lhs.security < rhs.security)
        return -1;
    else if (lhs.security > rhs.security)
        return 1;

    // 'context' does not participate in the comparison

    return 0;
}

bool operator== (Port const& lhs, Port const& rhs) { return compare (lhs, rhs) == 0; }
bool operator!= (Port const& lhs, Port const& rhs) { return compare (lhs, rhs) != 0; }
bool operator<  (Port const& lhs, Port const& rhs) { return compare (lhs, rhs) <  0; }
bool operator<= (Port const& lhs, Port const& rhs) { return compare (lhs, rhs) <= 0; }
bool operator>  (Port const& lhs, Port const& rhs) { return compare (lhs, rhs) >  0; }
bool operator>= (Port const& lhs, Port const& rhs) { return compare (lhs, rhs) >= 0; }

}
}
