//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RPCDoor)

extern void initSSLContext (boost::asio::ssl::context& context,
                            std::string key_file, std::string cert_file, std::string chain_file);

RPCDoor::RPCDoor (boost::asio::io_service& io_service)
    : mAcceptor (io_service,
                 boost::asio::ip::tcp::endpoint (boost::asio::ip::address::from_string (theConfig.RPC_IP), theConfig.RPC_PORT))
    , mDelayTimer (io_service)
    , mSSLContext (boost::asio::ssl::context::sslv23)
{
    WriteLog (lsINFO, RPCDoor) << "RPC port: " << theConfig.RPC_IP << " " << theConfig.RPC_PORT << " allow remote: " << theConfig.RPC_ALLOW_REMOTE;

    if (theConfig.RPC_SECURE != 0)
        initSSLContext (mSSLContext, theConfig.RPC_SSL_KEY, theConfig.RPC_SSL_CERT, theConfig.RPC_SSL_CHAIN);

    startListening ();
}

RPCDoor::~RPCDoor ()
{
    WriteLog (lsINFO, RPCDoor) << "RPC port: " << theConfig.RPC_IP << " " << theConfig.RPC_PORT << " allow remote: " << theConfig.RPC_ALLOW_REMOTE;
}

void RPCDoor::startListening ()
{
    RPCServer::pointer new_connection = RPCServer::create (mAcceptor.get_io_service (), mSSLContext, &theApp->getOPs ());
    mAcceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

    mAcceptor.async_accept (new_connection->getRawSocket (),
                            boost::bind (&RPCDoor::handleConnect, this, new_connection,
                                         boost::asio::placeholders::error));
}

bool RPCDoor::isClientAllowed (const std::string& ip)
{
    if (theConfig.RPC_ALLOW_REMOTE)
        return true;

    if (ip == "127.0.0.1")
        return true;

    return false;
}

void RPCDoor::handleConnect (RPCServer::pointer new_connection, const boost::system::error_code& error)
{
    bool delay = false;

    if (!error)
    {
        // Restrict callers by IP
        try
        {
            if (!isClientAllowed (new_connection->getRawSocket ().remote_endpoint ().address ().to_string ()))
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

        WriteLog (lsINFO, RPCDoor) << "RPCDoor::handleConnect Error: " << error;
    }

    if (delay)
    {
        mDelayTimer.expires_from_now (boost::posix_time::milliseconds (1000));
        mDelayTimer.async_wait (boost::bind (&RPCDoor::startListening, this));
    }
    else
        startListening ();
}
// vim:ts=4
