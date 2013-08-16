//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

MultiSocket::Options::Options (Flags flags)
    : useClientSsl (false)
    , enableServerSsl (false)
    , requireServerSsl (false)
    , requireServerProxy (false)
{
    setFromFlags (flags);
}

void MultiSocket::Options::setFromFlags (Flags flags)
{
   useClientSsl = (flags & client_ssl) != 0;
   enableServerSsl = (flags & (server_ssl | server_ssl_required)) != 0;
   requireServerSsl = (flags & server_ssl_required) != 0;
   requireServerProxy = (flags & server_proxy) !=0;
}

//------------------------------------------------------------------------------

MultiSocket* MultiSocket::New (boost::asio::io_service& io_service, int flags)
{
    return new MultiSocketType <boost::asio::ip::tcp::socket> (io_service, flags);
}

SslContextBase::BoostContextType &MultiSocket::getRippleTlsBoostContext ()
{
    static ScopedPointer <RippleTlsContext> context (RippleTlsContext::New ());

    return context->getBoostContext ();
}

//------------------------------------------------------------------------------

class MultiSocketTests : public UnitTest
{
public:
    class MultiSocketDetails : public TestPeerDetails
    {
    public:
        typedef int arg_type;

        MultiSocketDetails (int flags)
            : m_flags (flags)
        {
        }

        static String getArgName (arg_type arg)
        {
            String s;

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

            if (s != String::empty)
            {
                s = "(" + s.substring (0, s.length () - 1) + ")";
            }

            return s;
        }

        String name ()
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

        explicit MultiSocketDetailsType (arg_type flags)
            : MultiSocketDetails (flags)
            , m_socket (get_io_service ())
            , m_acceptor (get_io_service ())
            , m_multiSocket (m_socket, flags)
            , m_acceptor_wrapper (m_acceptor)
        {
        }

        Socket& get_socket ()
        {
            return m_multiSocket;
        }

        Socket& get_acceptor ()
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
            if (role == PeerRole::server)
                return endpoint_type (boost::asio::ip::tcp::v6 (), 1052);
            else
                return endpoint_type (boost::asio::ip::address_v6 ().from_string ("::1"), 1052);
        }

    protected:
        socket_type m_socket;
        acceptor_type m_acceptor;
        MultiSocketType <socket_type&> m_multiSocket;
        SocketWrapper <acceptor_type&> m_acceptor_wrapper;
    };

    //--------------------------------------------------------------------------

    template <typename Protocol, typename ClientArg, typename ServerArg>
    void run_async (ClientArg const& clientArg, ServerArg const& serverArg)
    {
        PeerTest::run <MultiSocketDetailsType <Protocol>,
            TestPeerLogicAsyncClient, TestPeerLogicAsyncServer> (clientArg, serverArg, timeoutSeconds).report (*this);
    }

    template <typename Protocol, typename ClientArg, typename ServerArg>
    void run (ClientArg const& clientArg, ServerArg const& serverArg)
    {
        PeerTest::run <MultiSocketDetailsType <Protocol>,
            TestPeerLogicSyncClient, TestPeerLogicSyncServer> (clientArg, serverArg, timeoutSeconds).report (*this);

        PeerTest::run <MultiSocketDetailsType <Protocol>,
            TestPeerLogicAsyncClient, TestPeerLogicSyncServer> (clientArg, serverArg, timeoutSeconds).report (*this);

        PeerTest::run <MultiSocketDetailsType <Protocol>,
            TestPeerLogicSyncClient, TestPeerLogicAsyncServer> (clientArg, serverArg, timeoutSeconds).report (*this);

        run_async <Protocol> (clientArg, serverArg);
    }

    //--------------------------------------------------------------------------

    template <typename Protocol>
    void testFlags (int extraClientFlags, int extraServerFlags)
    {
        check_precondition (! MultiSocket::Flag (extraClientFlags).any_set (MultiSocket::Flag::client_role | MultiSocket::Flag::server_role));

#if 0
        run <Protocol> (MultiSocket::Flag::client_role | extraClientFlags, MultiSocket::Flag::server_role | extraServerFlags);
#else
        run_async <Protocol> (MultiSocket::Flag::client_role | extraClientFlags, MultiSocket::Flag::server_role | extraServerFlags);
#endif
    }

    template <typename Protocol>
    void testProtocol ()
    {
#if 0
        // These should pass.
        run <Protocol> (0, 0);
        run <Protocol> (MultiSocket::Flag::client_role, 0);
        run <Protocol> (0, MultiSocket::Flag::server_role);
        run <Protocol> (MultiSocket::Flag::client_role, MultiSocket::Flag::server_role);
#endif

#if 0
        // These should pass
        testFlags <Protocol> (MultiSocket::Flag::ssl, MultiSocket::Flag::ssl_required);
        testFlags <Protocol> (0, MultiSocket::Flag::ssl);
#endif
        testFlags <Protocol> (MultiSocket::Flag::ssl, MultiSocket::Flag::ssl);
    }

    void runTest ()
    {
        testProtocol <boost::asio::ip::tcp> ();
    }

    //--------------------------------------------------------------------------

    enum
    {
        timeoutSeconds = 10
    };

    MultiSocketTests () : UnitTest ("MultiSocket", "ripple", runManual)
    {
    }
};

static MultiSocketTests multiSocketTests;

