//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (PeerDoor)

class PeerDoorImp : public PeerDoor, LeakChecked <PeerDoorImp>
{
public:
    PeerDoorImp (std::string const& ip,
                int port,
                std::string const& sslCiphers,
                boost::asio::io_service& io_service)
        : mAcceptor (
            io_service,
            boost::asio::ip::tcp::endpoint (boost::asio::ip::address ().from_string (ip.empty () ? "0.0.0.0" : ip),
            port))
        , mCtx (boost::asio::ssl::context::sslv23)
        , mDelayTimer (io_service)
    {
        mCtx.set_options (
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::single_dh_use);

        SSL_CTX_set_tmp_dh_callback (mCtx.native_handle (), handleTmpDh);

        if (SSL_CTX_set_cipher_list (mCtx.native_handle (), sslCiphers.c_str ()) != 1)
            std::runtime_error ("Error setting cipher list (no valid ciphers).");

        if (! ip.empty () && port != 0)
        {
            Log (lsINFO) << "Peer port: " << ip << " " << port;
            startListening ();
        }
    }

    //--------------------------------------------------------------------------

    boost::asio::ssl::context& getSSLContext ()
    {
        return mCtx;
    }

    //--------------------------------------------------------------------------

    void startListening ()
    {
        Peer::pointer new_connection = Peer::New (
                                           mAcceptor.get_io_service (),
                                           mCtx,
                                           getApp().getPeers ().assignPeerId (),
                                           true);

        mAcceptor.async_accept (new_connection->getSocket (),
                                boost::bind (&PeerDoorImp::handleConnect, this, new_connection,
                                             boost::asio::placeholders::error));
    }

    //--------------------------------------------------------------------------

    void handleConnect (Peer::pointer new_connection,
                                  const boost::system::error_code& error)
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
    boost::asio::ip::tcp::acceptor  mAcceptor;
    boost::asio::ssl::context       mCtx;
    boost::asio::deadline_timer     mDelayTimer;
};

//------------------------------------------------------------------------------

PeerDoor* PeerDoor::New (
    std::string const& ip,
    int port,
    std::string const& sslCiphers,
    boost::asio::io_service& io_service)
{
    return new PeerDoorImp (ip, port, sslCiphers, io_service);
}
