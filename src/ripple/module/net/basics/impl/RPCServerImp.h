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

#include <beast/asio/IPAddressConversion.h>

namespace ripple {

SETUP_LOG (RPCServer)

class RPCServerImp
    : public RPCServer
    , public std::enable_shared_from_this <RPCServerImp>
    , public beast::LeakChecked <RPCServerImp>
{
public:
    typedef std::shared_ptr <RPCServerImp> pointer;

    RPCServerImp (
        boost::asio::io_service& io_service,
        boost::asio::ssl::context& context,
        Handler& handler)
        : m_handler (handler)
        , mStrand (io_service)
        , mSocket (io_service, context)
    {
    }

    //--------------------------------------------------------------------------

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
            mStrand.wrap (std::bind (
                &RPCServerImp::handle_read_line,
                std::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                beast::asio::placeholders::error)));
    }

    //--------------------------------------------------------------------------

    void handle_write (const boost::system::error_code& e)
    {
        if (!e)
        {
            HTTPRequest::Action action = mHTTPRequest.requestDone (false);

            if (action == HTTPRequest::haCLOSE_CONN)
            {
                mSocket.async_shutdown (mStrand.wrap (std::bind (
                    &RPCServerImp::handle_shutdown,
                    std::static_pointer_cast <RPCServerImp> (shared_from_this()),
                    beast::asio::placeholders::error)));
            }
            else
            {
                boost::asio::async_read_until (
                    mSocket,
                    mLineBuffer,
                    "\r\n",
                    mStrand.wrap (std::bind (
                        &RPCServerImp::handle_read_line,
                        std::static_pointer_cast <RPCServerImp> (shared_from_this()),
                        beast::asio::placeholders::error)));
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

                mSocket.async_shutdown (mStrand.wrap (std::bind (
                    &RPCServerImp::handle_shutdown,
                    std::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                    beast::asio::placeholders::error)));
            }
            else if (action == HTTPRequest::haREAD_LINE)
            {
                boost::asio::async_read_until (
                    mSocket,
                    mLineBuffer,
                    "\r\n",
                    mStrand.wrap (std::bind (
                        &RPCServerImp::handle_read_line,
                        std::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                        beast::asio::placeholders::error)));
            }
            else if (action == HTTPRequest::haREAD_RAW)
            {
                int rLen = mHTTPRequest.getDataSize ();

                if ((rLen < 0) || (rLen > maxQueryBytes))
                {
                    WriteLog (lsWARNING, RPCServer) << "Illegal RPC request length " << rLen;

                    mSocket.async_shutdown (mStrand.wrap (std::bind (
                        &RPCServerImp::handle_shutdown,
                        std::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                        beast::asio::placeholders::error)));
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
                            mStrand.wrap (std::bind (
                                &RPCServerImp::handle_read_req,
                                std::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                                beast::asio::placeholders::error)));

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
                mSocket.async_shutdown (mStrand.wrap (std::bind (
                    &RPCServerImp::handle_shutdown,
                    std::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                    beast::asio::placeholders::error)));
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
            mStrand.wrap (std::bind (
                &RPCServerImp::handle_write,
                std::static_pointer_cast <RPCServerImp> (shared_from_this ()),
                beast::asio::placeholders::error)));
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
    
        return m_handler.processRequest (request, 
            beast::IPAddressConversion::from_asio (
                m_remote_endpoint.address()));
    }

    //--------------------------------------------------------------------------

    AutoSocket& getSocket ()
    {
        return mSocket;
    }

    //--------------------------------------------------------------------------

    boost::asio::ip::tcp::socket& getRawSocket ()
    {
        return mSocket.PlainSocket ();
    }

    boost::asio::ip::tcp::socket::endpoint_type& getRemoteEndpoint ()
    {
        return m_remote_endpoint;
    }

private:
    Handler& m_handler;

    boost::asio::io_service::strand mStrand;
    AutoSocket mSocket;
    AutoSocket::endpoint_type m_remote_endpoint;

    boost::asio::streambuf mLineBuffer;
    Blob mQueryVec;
    std::string mReplyStr;

    HTTPRequest mHTTPRequest;
};

} // ripple
