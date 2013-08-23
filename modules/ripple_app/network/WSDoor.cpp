//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (WSDoor)

//
// This is a light weight, untrusted interface for web clients.
// For now we don't provide proof.  Later we will.
//
// Might need to support this header for browsers: Access-Control-Allow-Origin: *
// - https://developer.mozilla.org/en-US/docs/HTTP_access_control
//

//
// Strategy:
// - We only talk to NetworkOPs (so we will work even in thin mode)
// - NetworkOPs is smart enough to subscribe and or pass back messages
//
// VFALCO NOTE NetworkOPs isn't used here...
//

WSDoor::WSDoor (std::string const& strIp, int iPort, bool bPublic)
    : Thread ("websocket")
    , m_endpointLock (this, "WSDoor", __FILE__, __LINE__)
    , mPublic (bPublic)
    , mIp (strIp)
    , mPort (iPort)
{
    startThread ();
}

WSDoor::~WSDoor ()
{
    {
        ScopedLockType lock (m_endpointLock, __FILE__, __LINE__);

        if (m_endpoint != nullptr)
            m_endpoint->stop ();
    }

    signalThreadShouldExit ();
    waitForThreadToExit ();
}

void WSDoor::run ()
{
    WriteLog (lsINFO, WSDoor) << boost::str (boost::format ("Websocket: %s: Listening: %s %d ")
                                        % (mPublic ? "Public" : "Private")
                                        % mIp
                                        % mPort);

    // Generate a single SSL context for use by all connections.
    boost::shared_ptr<boost::asio::ssl::context>    mCtx;
    mCtx    = boost::make_shared<boost::asio::ssl::context> (boost::asio::ssl::context::sslv23);

    mCtx->set_options (
        boost::asio::ssl::context::default_workarounds
        | boost::asio::ssl::context::no_sslv2
        | boost::asio::ssl::context::single_dh_use);

    SSL_CTX_set_tmp_dh_callback (mCtx->native_handle (), handleTmpDh);

    websocketpp::server_autotls::handler::ptr   handler (new WSServerHandler<websocketpp::server_autotls> (mCtx, mPublic));

    {
        ScopedLockType lock (m_endpointLock, __FILE__, __LINE__);

        m_endpoint = new websocketpp::server_autotls (handler);
    }

    // mEndpoint->alog().unset_level(websocketpp::log::alevel::ALL);
    // mEndpoint->elog().unset_level(websocketpp::log::elevel::ALL);

    // Call the main-event-loop of the websocket server.
    try
    {
        m_endpoint->listen (
            boost::asio::ip::tcp::endpoint (
                boost::asio::ip::address ().from_string (mIp), mPort));
    }
    catch (websocketpp::exception& e)
    {
        WriteLog (lsWARNING, WSDoor) << "websocketpp exception: " << e.what ();

        while (1) // temporary workaround for websocketpp throwing exceptions on access/close races
        {
            // https://github.com/zaphoyd/websocketpp/issues/98
            try
            {
                m_endpoint->get_io_service ().run ();
                break;
            }
            catch (websocketpp::exception& e)
            {
                WriteLog (lsWARNING, WSDoor) << "websocketpp exception: " << e.what ();
            }
        }
    }

    delete m_endpoint;
}

void WSDoor::stop ()
{
    {
        ScopedLockType lock (m_endpointLock, __FILE__, __LINE__);

        if (m_endpoint != nullptr)
            m_endpoint->stop ();
    }

    signalThreadShouldExit ();
    waitForThreadToExit ();
}
