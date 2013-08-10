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

MultiSocket* MultiSocket::New (boost::asio::io_service& io_service,
                                           Options const& options)
{
    return new MultiSocketType <boost::asio::ip::tcp::socket> (io_service, options);
}

//------------------------------------------------------------------------------

class MultiSocketTests : public UnitTest
{
public:
    class MultiSocketDetails : public TestPeerDetails
    {
    public:
        typedef int arg_type;

        // These flags get combined to determine the multisocket attributes
        //
        enum Flags
        {
            none                = 0,
            client_ssl          = 1,
            server_ssl          = 2,
            server_ssl_required = 4,
            server_proxy        = 8,

            // these are for producing the endpoint test parameters
            tcpv4               = 16,
            tcpv6               = 32
        };

        MultiSocketDetails (arg_type flags)
            : m_flags (flags)
        {
            m_socketOptions.useClientSsl = (flags & client_ssl) != 0;
            m_socketOptions.enableServerSsl = (flags & (server_ssl | server_ssl_required)) != 0;
            m_socketOptions.requireServerSsl = (flags & server_ssl_required) != 0;
            m_socketOptions.requireServerProxy = (flags & server_proxy) !=0;
        }

        static String getArgName (arg_type arg)
        {
            String s;
            if (arg & tcpv4) s << "tcpv4:";
            if (arg & tcpv6) s << "tcpv6:";
            if (arg != 0)
            {
                s << "[";
                if (arg & client_ssl)           s << "client_ssl,";
                if (arg & server_ssl)           s << "server_ssl,";
                if (arg & server_ssl_required)  s << "server_ssl_required,";
                if (arg & server_proxy)         s << "server_proxy,";
                s = s.substring (0, s.length () - 1) + "]";
            }
            else
            {
                s = "[plain]";
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

        MultiSocket::Options const& getSocketOptions () const noexcept
        {
            return m_socketOptions;
        }

    protected:
        arg_type m_flags;
        MultiSocket::Options m_socketOptions;
    };

    //--------------------------------------------------------------------------

    template <class InternetProtocol>
    class MultiSocketDetailsType : public MultiSocketDetails
    {
    protected:
        typedef InternetProtocol                 protocol_type;
        typedef typename protocol_type::socket   socket_type;
        typedef typename protocol_type::acceptor acceptor_type;
        typedef typename protocol_type::endpoint endpoint_type;
        typedef typename protocol_type::resolver resolver_type;

    public:
        typedef socket_type             native_socket_type;
        typedef acceptor_type           native_acceptor_type;

        explicit MultiSocketDetailsType (arg_type flags = none)
            : MultiSocketDetails (flags)
            , m_socket (get_io_service ())
            , m_acceptor (get_io_service ())
            , m_multiSocket (m_socket, getSocketOptions ())
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

        endpoint_type get_endpoint (TestPeer::Role role)
        {
            if (getFlags () & MultiSocketDetails::tcpv6)
            {
                if (role == TestPeer::Role::server)
                    return endpoint_type (boost::asio::ip::tcp::v6 (), 1052);
                else
                    return endpoint_type (boost::asio::ip::address_v6 ().from_string ("::1"), 1052);
            }
            else
            {
                if (role == TestPeer::Role::server)
                    return endpoint_type (boost::asio::ip::address_v4::any (), 1053);
                else
                    return endpoint_type (boost::asio::ip::address_v4::loopback (), 1053);
            }
        }

    protected:
        socket_type m_socket;
        acceptor_type m_acceptor;
        MultiSocketType <socket_type&> m_multiSocket;
        SocketWrapper <acceptor_type> m_acceptor_wrapper;
    };

    MultiSocketTests () : UnitTest ("MultiSocket", "ripple", runManual)
    {
    }

    enum
    {
        timeoutSeconds = 3
    };

    template <typename Details, class Arg>
    void testAsync (Arg const& arg)
    {
        TestPeerTestType::run <Details, TestPeerLogicAsyncServer, TestPeerLogicAsyncClient, Arg>
            (arg, timeoutSeconds).report (*this);
    }

    template <typename InternetProtocol, class Arg>
    void testProtocol (Arg const& arg)
    {
        testAsync <MultiSocketDetailsType <InternetProtocol> > (arg);
    }

    void testOptions (int flags)
    {
        testProtocol <boost::asio::ip::tcp> (flags);
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        // These should pass
        testOptions (MultiSocketDetails::none);
        testOptions (MultiSocketDetails::server_ssl);
        testOptions (MultiSocketDetails::client_ssl | MultiSocketDetails::server_ssl);
        testOptions (MultiSocketDetails::client_ssl | MultiSocketDetails::server_ssl_required);

        // These should fail
        testOptions (MultiSocketDetails::client_ssl);
        testOptions (MultiSocketDetails::server_ssl_required);
    }
};

static MultiSocketTests multiSocketTests;

