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

#include "../impl/MultiSocketImpl.h"

#include "../RippleSSLContext.h"

#include "../../beast/beast/unit_test/suite.h"

#include <cassert>

namespace ripple {

class MultiSocket_test : public beast::unit_test::suite
{
public:
    class MultiSocketDetails : public beast::asio::TestPeerDetails
    {
    public:
        typedef int arg_type;

        MultiSocketDetails (int flags)
            : m_flags (flags)
        {
        }

        static beast::String getArgName (arg_type arg)
        {
            beast::String s;

            if (arg & MultiSocket::Flag::client_role)
                s << "client,";

            if (arg & MultiSocket::Flag::server_role)
                s << "server,";

            if (arg & MultiSocket::Flag::ssl)
                s << "ssl,";

            if (arg & MultiSocket::Flag::ssl_required)
                s << "ssl_required,";

            if (arg & MultiSocket::Flag::proxy)
                s << "proxy,";

            if (s != beast::String::empty)
            {
                s = "(" + s.substring (0, s.length () - 1) + ")";
            }

            return s;
        }

        static boost::asio::ssl::context& getSSLContext ()
        {
            struct ContextHolder
            {
                ContextHolder ()
                    : context (RippleSSLContext::createAnonymous (
                        "ALL:!LOW:!EXP:!MD5:@STRENGTH"))
                {
                    // VFALCO NOTE Not sure if this is needed?
                    context->get().set_verify_mode (
                        boost::asio::ssl::verify_none);
                }

                std::unique_ptr <RippleSSLContext> context;
            };

            static ContextHolder holder;

            return holder.context->get ();
        }

        beast::String name () const
        {
            return getArgName (m_flags);
        }

        arg_type getFlags () const noexcept
        {
            return m_flags;
        }

    protected:
        arg_type m_flags;
    };

    //--------------------------------------------------------------------------

    template <class Protocol>
    class MultiSocketDetailsType : public MultiSocketDetails
    {
    protected:
        typedef Protocol                         protocol_type;
        typedef typename protocol_type::socket   socket_type;
        typedef typename protocol_type::acceptor acceptor_type;
        typedef typename protocol_type::endpoint endpoint_type;
        typedef typename protocol_type::resolver resolver_type;

    public:
        typedef socket_type             native_socket_type;
        typedef acceptor_type           native_acceptor_type;

        MultiSocketDetailsType (arg_type flags)
            : MultiSocketDetails (flags)
            , m_socket (get_io_service ())
            , m_acceptor (get_io_service ())
            , m_multiSocket (MultiSocketDetails::getSSLContext (),
                flags, m_socket)
            , m_acceptor_wrapper (m_acceptor)
        {
        }

        beast::asio::abstract_socket&
        get_socket ()
        {
            return m_multiSocket;
        }

        beast::asio::abstract_socket&
        get_acceptor ()
        {
            return m_acceptor_wrapper;
        }

        socket_type&
        get_native_socket ()
        {
            return m_socket;
        }

        acceptor_type&
        get_native_acceptor ()
        {
            return m_acceptor;
        }

        endpoint_type
        get_endpoint (beast::asio::PeerRole role)
        {
            if (role == beast::asio::PeerRole::server)
                return endpoint_type (boost::asio::ip::tcp::v6 (), 1052);
            else
                return endpoint_type (boost::asio::ip::address_v6 ().from_string ("::1"), 1052);
        }

    protected:
        socket_type m_socket;
        acceptor_type m_acceptor;
        MultiSocketImpl <socket_type&> m_multiSocket;
        beast::asio::socket_wrapper <acceptor_type&> m_acceptor_wrapper;
    };

    //--------------------------------------------------------------------------
    
    template <typename Protocol, typename ClientArg, typename ServerArg>
    void runProxy (ClientArg const& clientArg, ServerArg const& serverArg)
    {
        beast::asio::PeerTest::run <MultiSocketDetailsType <Protocol>,
            beast::asio::TestPeerLogicProxyClient,
                beast::asio::TestPeerLogicSyncServer> (
                    clientArg, serverArg, timeoutSeconds).report (*this);

        beast::asio::PeerTest::run <MultiSocketDetailsType <Protocol>,
            beast::asio::TestPeerLogicProxyClient,
                beast::asio::TestPeerLogicAsyncServer> (
                    clientArg, serverArg, timeoutSeconds).report (*this);
    }

    //--------------------------------------------------------------------------

    template <typename Protocol, typename ClientArg, typename ServerArg>
    void run (ClientArg const& clientArg, ServerArg const& serverArg)
    {
        beast::asio::PeerTest::run <MultiSocketDetailsType <Protocol>,
            beast::asio::TestPeerLogicSyncClient,
                beast::asio::TestPeerLogicSyncServer>
                    (clientArg, serverArg, timeoutSeconds).report (*this);

        beast::asio::PeerTest::run <MultiSocketDetailsType <Protocol>,
            beast::asio::TestPeerLogicAsyncClient,
                beast::asio::TestPeerLogicSyncServer>
                    (clientArg, serverArg, timeoutSeconds).report (*this);

        beast::asio::PeerTest::run <MultiSocketDetailsType <Protocol>,
            beast::asio::TestPeerLogicSyncClient,
                beast::asio::TestPeerLogicAsyncServer>
                    (clientArg, serverArg, timeoutSeconds).report (*this);

        beast::asio::PeerTest::run <MultiSocketDetailsType <Protocol>,
            beast::asio::TestPeerLogicAsyncClient,
                beast::asio::TestPeerLogicAsyncServer>
                    (clientArg, serverArg, timeoutSeconds).report (*this);
    }

    //--------------------------------------------------------------------------

    template <typename Protocol>
    void testProxyFlags (int extraClientFlags, int extraServerFlags)
    {
        assert (! MultiSocket::Flag (extraClientFlags).any_set (
            MultiSocket::Flag::client_role | MultiSocket::Flag::server_role));

        runProxy <Protocol> (MultiSocket::Flag::client_role | extraClientFlags,
                             MultiSocket::Flag::server_role | extraServerFlags);
    }

    //--------------------------------------------------------------------------

    template <typename Protocol>
    void testFlags (int extraClientFlags, int extraServerFlags)
    {
        assert (! MultiSocket::Flag (extraClientFlags).any_set (
            MultiSocket::Flag::client_role | MultiSocket::Flag::server_role));

        run <Protocol> (MultiSocket::Flag::client_role | extraClientFlags,
                        MultiSocket::Flag::server_role | extraServerFlags);
    }

    template <typename Protocol>
    void testProtocol ()
    {
        // Simple tests
        run <Protocol> (0,
                        0);

        run <Protocol> (MultiSocket::Flag::client_role,
                        0);

        run <Protocol> (0, MultiSocket::Flag::server_role);

        run <Protocol> (MultiSocket::Flag::client_role,
                        MultiSocket::Flag::server_role);

        testFlags <Protocol> (MultiSocket::Flag::ssl,
                              MultiSocket::Flag::ssl_required);
        // SSL-Detect tests
        testFlags <Protocol> (0,
                              MultiSocket::Flag::ssl);

        testFlags <Protocol> (MultiSocket::Flag::ssl,
                              MultiSocket::Flag::ssl);

        // PROXY Handshake tests
        testProxyFlags <Protocol> (MultiSocket::Flag::proxy,
                                   MultiSocket::Flag::proxy);

        testProxyFlags <Protocol> (MultiSocket::Flag::proxy | MultiSocket::Flag::ssl,
                                   MultiSocket::Flag::proxy | MultiSocket::Flag::ssl_required);

        // PROXY + SSL-Detect tests
        testProxyFlags <Protocol> (MultiSocket::Flag::proxy,
                                   MultiSocket::Flag::proxy | MultiSocket::Flag::ssl);

        testProxyFlags <Protocol> (MultiSocket::Flag::proxy | MultiSocket::Flag::ssl,
                                   MultiSocket::Flag::proxy | MultiSocket::Flag::ssl);
    }

    void run ()
    {
        testProtocol <boost::asio::ip::tcp> ();
    }

    //--------------------------------------------------------------------------

    enum
    {
        timeoutSeconds = 10
    };
};

BEAST_DEFINE_TESTSUITE(MultiSocket,common,ripple);

}
