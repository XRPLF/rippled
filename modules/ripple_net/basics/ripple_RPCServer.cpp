//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RPCServer)

class RPCServerImp : public RPCServer, LeakChecked <RPCServerImp>
{
public:
    RPCServerImp (
        boost::asio::io_service& io_service,
        boost::asio::ssl::context& context,
        Handler& handler)
        : m_handler (handler)
        , mSocket (io_service, context)
        , mStrand (io_service)
#if RIPPLE_USES_BEAST_SOCKETS
        , m_socketWrapper (mSocket)
#endif

    {
    }

    //--------------------------------------------------------------------------
private:
    enum
    {
        maxQueryBytes = 1024 * 1024
    };

    void connected ()
    {
        boost::asio::async_read_until (
            mSocket,
            mLineBuffer,
            "\r\n",
            mStrand.wrap (boost::bind (
                &RPCServerImp::handle_read_line,
                boost::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                boost::asio::placeholders::error)));
    }

    //--------------------------------------------------------------------------

    void handle_write (const boost::system::error_code& e)
    {
        if (!e)
        {
            HTTPRequest::Action action = mHTTPRequest.requestDone (false);

            if (action == HTTPRequest::haCLOSE_CONN)
            {
                mSocket.async_shutdown (mStrand.wrap (boost::bind (
                    &RPCServerImp::handle_shutdown,
                    boost::static_pointer_cast <RPCServerImp> (shared_from_this()),
                    boost::asio::placeholders::error)));
            }
            else
            {
                boost::asio::async_read_until (
                    mSocket,
                    mLineBuffer,
                    "\r\n",
                    mStrand.wrap (boost::bind (
                        &RPCServerImp::handle_read_line,
                        boost::static_pointer_cast <RPCServerImp> (shared_from_this()),
                        boost::asio::placeholders::error)));
            }
        }

        if (e != boost::asio::error::operation_aborted)
        {
            // VFALCO TODO What is this for? It was commented out.
            //
            //connection_manager_.stop (shared_from_this ());
        }
    }

    //--------------------------------------------------------------------------

    void handle_read_line (const boost::system::error_code& e)
    {
        if (! e)
        {
            HTTPRequest::Action action = mHTTPRequest.consume (mLineBuffer);

            if (action == HTTPRequest::haDO_REQUEST)
            {
                // request with no body
                WriteLog (lsWARNING, RPCServer) << "RPC HTTP request with no body";

                mSocket.async_shutdown (mStrand.wrap (boost::bind (
                    &RPCServerImp::handle_shutdown,
                    boost::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                    boost::asio::placeholders::error)));
            }
            else if (action == HTTPRequest::haREAD_LINE)
            {
                boost::asio::async_read_until (
                    mSocket,
                    mLineBuffer,
                    "\r\n",
                    mStrand.wrap (boost::bind (
                        &RPCServerImp::handle_read_line,
                        boost::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                        boost::asio::placeholders::error)));
            }
            else if (action == HTTPRequest::haREAD_RAW)
            {
                int rLen = mHTTPRequest.getDataSize ();

                if ((rLen < 0) || (rLen > maxQueryBytes))
                {
                    WriteLog (lsWARNING, RPCServer) << "Illegal RPC request length " << rLen;

                    mSocket.async_shutdown (mStrand.wrap (boost::bind (
                        &RPCServerImp::handle_shutdown,
                        boost::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                        boost::asio::placeholders::error)));
                }
                else
                {
                    int alreadyHave = mLineBuffer.size ();

                    if (alreadyHave < rLen)
                    {
                        mQueryVec.resize (rLen - alreadyHave);

                        boost::asio::async_read (
                            mSocket,
                            boost::asio::buffer (mQueryVec),
                            mStrand.wrap (boost::bind (
                                &RPCServerImp::handle_read_req,
                                boost::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                                boost::asio::placeholders::error)));

                        WriteLog (lsTRACE, RPCServer) << "Waiting for completed request: " << rLen;
                    }
                    else
                    {
                        // we have the whole thing
                        mQueryVec.resize (0);

                        handle_read_req (e);
                    }
                }
            }
            else
            {
                mSocket.async_shutdown (mStrand.wrap (boost::bind (
                    &RPCServerImp::handle_shutdown,
                    boost::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                    boost::asio::placeholders::error)));
            }
        }
    }

    //--------------------------------------------------------------------------

    void handle_read_req (const boost::system::error_code& ec)
    {
        std::string req;

        if (mLineBuffer.size ())
        {
            req.assign (boost::asio::buffer_cast <const char*> (mLineBuffer.data ()), mLineBuffer.size ());

            mLineBuffer.consume (mLineBuffer.size ());
        }

        req += strCopy (mQueryVec);

        if (! m_handler.isAuthorized (mHTTPRequest.peekHeaders ()))
        {
            mReplyStr = m_handler.createResponse (403, "Forbidden");
        }
        else
        {
            mReplyStr = handleRequest (req);
        }

        boost::asio::async_write (
            mSocket,
            boost::asio::buffer (mReplyStr),
            mStrand.wrap (boost::bind (
                &RPCServerImp::handle_write,
                boost::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                boost::asio::placeholders::error)));
    }

    //--------------------------------------------------------------------------

    void handle_shutdown (const boost::system::error_code& ec)
    {
        // nothing to do, we just keep the object alive
    }

    //--------------------------------------------------------------------------

    // JSON-RPC request must contain "method", "params", and "id" fields.
    //
    std::string handleRequest (const std::string& request)
    {
        WriteLog (lsTRACE, RPCServer) << "handleRequest " << request;

        // Figure out the remote address.
        // VFALCO TODO Clean up this try/catch nonsense.
        //
        std::string remoteAddress;

        try
        {
            remoteAddress = mSocket.PlainSocket ().remote_endpoint ().address ().to_string ();
        }
        catch (...)
        {
            // endpoint already disconnected
            return "";
        }

        return m_handler.processRequest (request, remoteAddress);
    }

    //--------------------------------------------------------------------------

#if RIPPLE_USES_BEAST_SOCKETS
    Socket& getSocket ()
    {
        return m_socketWrapper;
    }
#else
    AutoSocket& getSocket ()
    {
        return mSocket;
    }
#endif

    //--------------------------------------------------------------------------

    boost::asio::ip::tcp::socket& getRawSocket ()
    {
        return mSocket.PlainSocket ();
    }

    //--------------------------------------------------------------------------

    std::string getRemoteAddressText ()
    {
        std::string address;

        address = mSocket.PlainSocket ().remote_endpoint ().address ().to_string ();

        return address;
    }

private:
    Handler& m_handler;

    AutoSocket mSocket;
#if RIPPLE_USES_BEAST_SOCKETS
    SocketWrapper <AutoSocket> m_socketWrapper;
#endif
    boost::asio::io_service::strand mStrand;

    boost::asio::streambuf mLineBuffer;
    Blob mQueryVec;
    std::string mReplyStr;

    HTTPRequest mHTTPRequest;
};

//------------------------------------------------------------------------------

RPCServer::pointer RPCServer::New (
    boost::asio::io_service& io_service,
    boost::asio::ssl::context& context,
    Handler& handler)
{
    return pointer (new RPCServerImp (io_service, context, handler));
}
