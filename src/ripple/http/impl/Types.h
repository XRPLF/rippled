//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_TYPES_H_INCLUDED
#define RIPPLE_HTTP_TYPES_H_INCLUDED

namespace ripple {
namespace HTTP {

typedef boost::system::error_code error_code;
typedef boost::asio::ip::tcp Protocol;
typedef boost::asio::ip::address address;
typedef Protocol::endpoint endpoint_t;
typedef Protocol::acceptor acceptor;
typedef Protocol::socket socket;

inline std::string to_string (address const& addr)
{
    return addr.to_string();
}

inline std::string to_string (endpoint_t const& endpoint)
{
    std::stringstream ss;
    ss << to_string (endpoint.address());
    if (endpoint.port() != 0)
        ss << ":" << std::dec << endpoint.port();
    return std::string (ss.str());
}

inline endpoint_t to_asio (Port const& port)
{
    if (port.addr.isV4())
    {
        IPEndpoint::V4 v4 (port.addr.v4());
        std::string const& s (v4.to_string());
        return endpoint_t (address().from_string (s), port.port);
    }

    //IPEndpoint::V6 v6 (ep.v6());
    return endpoint_t ();
}

inline IPEndpoint from_asio (endpoint_t const& endpoint)
{
    std::stringstream ss (to_string (endpoint));
    IPEndpoint ep;
    ss >> ep;
    return ep;
}

}
}

#endif
