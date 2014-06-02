//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_ASIO_TESTS_TESTPEERDETAILSTCP_H_INCLUDED
#define BEAST_ASIO_TESTS_TESTPEERDETAILSTCP_H_INCLUDED

#include <beast/asio/socket_wrapper.h>

namespace beast {
namespace asio {

/** Some predefined Detail classes for TestPeer */
struct TcpDetails : public TestPeerDetails
{
protected:
    typedef boost::asio::ip::tcp    protocol_type;
    typedef protocol_type::socket   socket_type;
    typedef protocol_type::acceptor acceptor_type;
    typedef protocol_type::endpoint endpoint_type;
    typedef protocol_type::resolver resolver_type;

public:
    typedef protocol_type arg_type;
    typedef socket_type   native_socket_type;
    typedef acceptor_type native_acceptor_type;

    explicit TcpDetails (arg_type protocol)
        : m_protocol (protocol)
        , m_socket (get_io_service ())
        , m_acceptor (get_io_service ())
        , m_socket_wrapper (m_socket)
        , m_acceptor_wrapper (m_acceptor)
    {
    }
    
    static String getArgName (arg_type arg)
    {
        if (arg == protocol_type::v4 ())
            return ".tcpv4";
        else if (arg == protocol_type::v6 ())
            return ".tcpv6";
        return ".tcp?";
    }

    String name () const
    {
        return getArgName (m_protocol);
    }

    abstract_socket& get_socket ()
    {
        return m_socket_wrapper;
    }

    abstract_socket& get_acceptor ()
    {
        return m_acceptor_wrapper;
    }

    socket_type& get_native_socket ()
    {
        return m_socket;
    }

    acceptor_type& get_native_acceptor ()
    {
        return m_acceptor;
    }

    endpoint_type get_endpoint (PeerRole role)
    {
        if (m_protocol == protocol_type::v4 ())
        {
            if (role == PeerRole::server)
                return endpoint_type (m_protocol, 1053);
            else
                return endpoint_type (boost::asio::ip::address_v4::loopback (), 1053);
        }
        else
        {
            if (role == PeerRole::server)
                return endpoint_type (m_protocol, 1052);
            else
                return endpoint_type (boost::asio::ip::address_v6 ().from_string ("::1"), 1052);
        }
    }

protected:
    protocol_type m_protocol;
    socket_type m_socket;
    acceptor_type m_acceptor;
    socket_wrapper <socket_type&> m_socket_wrapper;
    socket_wrapper <acceptor_type&> m_acceptor_wrapper;
};

}
}

#endif
