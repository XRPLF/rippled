//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RPCDoor)

class RPCDoorImp : public RPCDoor, public LeakChecked <RPCDoorImp>
{
public:
    RPCDoorImp (boost::asio::io_service& io_service, RPCServer::Handler& handler)
        : m_rpcServerHandler (handler)
        , mAcceptor (io_service,
                     boost::asio::ip::tcp::endpoint (boost::asio::ip::address::from_string (getConfig ().getRpcIP ()), getConfig ().getRpcPort ()))
        , mDelayTimer (io_service)
        , m_sslContext ((getConfig ().RPC_SECURE == 0) ?
                RippleSSLContext::createBare () :
                RippleSSLContext::createAuthenticated (
                    getConfig ().RPC_SSL_KEY,
                    getConfig ().RPC_SSL_CERT,
                    getConfig ().RPC_SSL_CHAIN))
    {
        WriteLog (lsINFO, RPCDoor) << "RPC port: " << getConfig ().getRpcAddress().toRawUTF8() << " allow remote: " << getConfig ().RPC_ALLOW_REMOTE;

        startListening ();
    }

    //--------------------------------------------------------------------------

    ~RPCDoorImp ()
    {
        WriteLog (lsINFO, RPCDoor) <<
            "RPC port: " << getConfig ().getRpcAddress().toRawUTF8() <<
            " allow remote: " << getConfig ().RPC_ALLOW_REMOTE;
    }

    //--------------------------------------------------------------------------

    void startListening ()
    {
        // VFALCO NOTE Why not use make_shared?
        RPCServerImp::pointer new_connection (boost::make_shared <RPCServerImp> (
            mAcceptor.get_io_service (), m_sslContext->get (), m_rpcServerHandler));

        mAcceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

        mAcceptor.async_accept (new_connection->getRawSocket (),
                                boost::bind (&RPCDoorImp::handleConnect, this, new_connection,
                                             boost::asio::placeholders::error));
    }

    //--------------------------------------------------------------------------

    bool isClientAllowed (const std::string& ip)
    {
        if (getConfig ().RPC_ALLOW_REMOTE)
            return true;

        // VFALCO TODO Represent ip addresses as a structure. Use isLoopback() member here
        //
        if (ip == "127.0.0.1")
            return true;

        return false;
    }

    //--------------------------------------------------------------------------

    void handleConnect (RPCServerImp::pointer new_connection, boost::system::error_code const& error)
    {
        bool delay = false;

        if (!error)
        {
            // Restrict callers by IP
            // VFALCO NOTE Prevent exceptions from being thrown at all.
            try
            {
                if (! isClientAllowed (new_connection->getRemoteAddressText ()))
                {
                    startListening ();
                    return;
                }
            }
            catch (...)
            {
                // client may have disconnected
                startListening ();
                return;
            }

            new_connection->getSocket ().async_handshake (AutoSocket::ssl_socket::server,
                    boost::bind (&RPCServer::connected, new_connection));
        }
        else
        {
            if (error == boost::system::errc::too_many_files_open)
                delay = true;

            WriteLog (lsINFO, RPCDoor) << "RPCDoorImp::handleConnect Error: " << error;
        }

        if (delay)
        {
            mDelayTimer.expires_from_now (boost::posix_time::milliseconds (1000));
            mDelayTimer.async_wait (boost::bind (&RPCDoorImp::startListening, this));
        }
        else
        {
            startListening ();
        }
    }

private:
    RPCServer::Handler& m_rpcServerHandler;
    boost::asio::ip::tcp::acceptor      mAcceptor;
    boost::asio::deadline_timer         mDelayTimer;
    ScopedPointer <RippleSSLContext>    m_sslContext;
};

//------------------------------------------------------------------------------

RPCDoor* RPCDoor::New (boost::asio::io_service& io_service, RPCServer::Handler& handler)
{
    ScopedPointer <RPCDoor> result (new RPCDoorImp (io_service, handler));

    return result.release ();
}
