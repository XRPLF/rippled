//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (PeerDoor)

// PEER_IP, PEER_PORT, PEER_SSL_CIPHER_LIST
PeerDoor::PeerDoor (
    std::string const& ip,
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

void PeerDoor::startListening ()
{
    Peer::pointer new_connection = Peer::New (
                                       mAcceptor.get_io_service (),
                                       mCtx,
                                       getApp().getPeers ().assignPeerId (),
                                       true);

    mAcceptor.async_accept (new_connection->getSocket (),
                            boost::bind (&PeerDoor::handleConnect, this, new_connection,
                                         boost::asio::placeholders::error));
}

void PeerDoor::handleConnect (Peer::pointer new_connection,
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
        mDelayTimer.async_wait (boost::bind (&PeerDoor::startListening, this));
    }
    else
    {
        startListening ();
    }
}

void initSSLContext (boost::asio::ssl::context& context,
                     std::string key_file, std::string cert_file, std::string chain_file)
{
    SSL_CTX* sslContext = context.native_handle ();

    context.set_options (boost::asio::ssl::context::default_workarounds |
                         boost::asio::ssl::context::no_sslv2 |
                         boost::asio::ssl::context::single_dh_use);

    bool cert_set = false;

    if (!cert_file.empty ())
    {
        boost::system::error_code error;
        context.use_certificate_file (cert_file, boost::asio::ssl::context::pem, error);

        if (error)
            throw std::runtime_error ("Unable to use certificate file");

        cert_set = true;
    }

    if (!chain_file.empty ())
    {
        // VFALCO Replace fopen() with RAII
        FILE* f = fopen (chain_file.c_str (), "r");

        if (!f)
            throw std::runtime_error ("Unable to open chain file");

        try
        {
            while (true)
            {
                X509* x = PEM_read_X509 (f, NULL, NULL, NULL);

                if (x == NULL)
                    break;

                if (!cert_set)
                {
                    if (SSL_CTX_use_certificate (sslContext, x) != 1)
                        throw std::runtime_error ("Unable to get certificate from chain file");

                    cert_set = true;
                }
                else if (SSL_CTX_add_extra_chain_cert (sslContext, x) != 1)
                {
                    X509_free (x);
                    throw std::runtime_error ("Unable to add chain certificate");
                }
            }

            fclose (f);
        }
        catch (...)
        {
            fclose (f);
            throw;
        }
    }

    if (!key_file.empty ())
    {
        boost::system::error_code error;
        context.use_private_key_file (key_file, boost::asio::ssl::context::pem, error);

        if (error)
            throw std::runtime_error ("Unable to use private key file");
    }

    if (SSL_CTX_check_private_key (sslContext) != 1)
        throw std::runtime_error ("Private key not valid");
}

// vim:ts=4
