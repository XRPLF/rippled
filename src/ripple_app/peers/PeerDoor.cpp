//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (PeerDoor)

class PeerDoorImp : public PeerDoor, LeakChecked <PeerDoorImp>
{
public:
    PeerDoorImp (Kind kind, std::string const& ip, int port,
        boost::asio::io_service& io_service, boost::asio::ssl::context& ssl_context)
        : m_kind (kind)
        , m_ssl_context (ssl_context)
        , mAcceptor (io_service, boost::asio::ip::tcp::endpoint (
            boost::asio::ip::address ().from_string (ip.empty () ? "0.0.0.0" : ip), port))
        , mDelayTimer (io_service)
    {
        if (! ip.empty () && port != 0)
        {
            Log (lsINFO) << "Peer port: " << ip << " " << port;
            startListening ();
        }
    }

    //--------------------------------------------------------------------------

    void startListening ()
    {
        bool const isInbound (true);
        bool const requirePROXYHandshake (m_kind == sslAndPROXYRequired);

        Peer::pointer new_connection (Peer::New (
            mAcceptor.get_io_service (), m_ssl_context,
                getApp().getPeers ().assignPeerId (),
                    isInbound, requirePROXYHandshake));

        mAcceptor.async_accept (new_connection->getNativeSocket (),
            boost::bind (&PeerDoorImp::handleConnect, this, new_connection,
                boost::asio::placeholders::error));
    }

    //--------------------------------------------------------------------------

    void handleConnect (Peer::pointer new_connection,
        boost::system::error_code const& error)
    {
        bool delay = false;

        if (!error)
        {
            new_connection->connected (error);
        }
        else
        {
            if (error == boost::system::errc::too_many_files_open)
                delay = true;

            WriteLog (lsERROR, PeerDoor) << error;
        }

        if (delay)
        {
            mDelayTimer.expires_from_now (boost::posix_time::milliseconds (500));
            mDelayTimer.async_wait (boost::bind (&PeerDoorImp::startListening, this));
        }
        else
        {
            startListening ();
        }
    }

private:
    Kind m_kind;
    boost::asio::ssl::context& m_ssl_context;
    boost::asio::ip::tcp::acceptor  mAcceptor;
    boost::asio::deadline_timer     mDelayTimer;
};

//------------------------------------------------------------------------------

PeerDoor* PeerDoor::New (Kind kind, std::string const& ip, int port,
    boost::asio::io_service& io_service, boost::asio::ssl::context& ssl_context)
{
    return new PeerDoorImp (kind, ip, port, io_service, ssl_context);
}
