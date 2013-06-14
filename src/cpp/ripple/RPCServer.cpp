#include "RPCServer.h"

#include "HttpsClient.h"
#include "RPC.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/read_until.hpp>

#ifndef RPC_MAXIMUM_QUERY
#define RPC_MAXIMUM_QUERY   (1024*1024)
#endif

SETUP_LOG (RPCServer)

RPCServer::RPCServer (boost::asio::io_service& io_service, boost::asio::ssl::context& context, NetworkOPs* nopNetwork)
    : mNetOps (nopNetwork), mSocket (io_service, context)
{
    mRole = RPCHandler::GUEST;
}

void RPCServer::connected ()
{
    //std::cerr << "RPC request" << std::endl;
    boost::asio::async_read_until (mSocket, mLineBuffer, "\r\n",
                                   boost::bind (&RPCServer::handle_read_line, shared_from_this (), boost::asio::placeholders::error));
}

void RPCServer::handle_read_req (const boost::system::error_code& e)
{
    std::string req;

    if (mLineBuffer.size ())
    {
        req.assign (boost::asio::buffer_cast<const char*> (mLineBuffer.data ()), mLineBuffer.size ());
        mLineBuffer.consume (mLineBuffer.size ());
    }

    req += strCopy (mQueryVec);

    if (!HTTPAuthorized (mHTTPRequest.peekHeaders ()))
        mReplyStr = HTTPReply (403, "Forbidden");
    else
        mReplyStr = handleRequest (req);

    boost::asio::async_write (mSocket, boost::asio::buffer (mReplyStr),
                              boost::bind (&RPCServer::handle_write, shared_from_this (), boost::asio::placeholders::error));
}

static void dummy_handler ()
{
    ;
}

void RPCServer::handle_read_line (const boost::system::error_code& e)
{
    if (e)
        return;

    HTTPRequestAction action = mHTTPRequest.consume (mLineBuffer);

    if (action == haDO_REQUEST)
    {
        // request with no body
        WriteLog (lsWARNING, RPCServer) << "RPC HTTP request with no body";
        mSocket.async_shutdown (boost::bind (&dummy_handler));
        return;
    }
    else if (action == haREAD_LINE)
    {
        boost::asio::async_read_until (mSocket, mLineBuffer, "\r\n",
                                       boost::bind (&RPCServer::handle_read_line, shared_from_this (),
                                               boost::asio::placeholders::error));
    }
    else if (action == haREAD_RAW)
    {
        int rLen = mHTTPRequest.getDataSize ();

        if ((rLen < 0) || (rLen > RPC_MAXIMUM_QUERY))
        {
            WriteLog (lsWARNING, RPCServer) << "Illegal RPC request length " << rLen;
            mSocket.async_shutdown (boost::bind (&dummy_handler));
            return;
        }

        int alreadyHave = mLineBuffer.size ();

        if (alreadyHave < rLen)
        {
            mQueryVec.resize (rLen - alreadyHave);
            boost::asio::async_read (mSocket, boost::asio::buffer (mQueryVec),
                                     boost::bind (&RPCServer::handle_read_req, shared_from_this (), boost::asio::placeholders::error));
            WriteLog (lsTRACE, RPCServer) << "Waiting for completed request: " << rLen;
        }
        else
        {
            // we have the whole thing
            mQueryVec.resize (0);
            handle_read_req (e);
        }
    }
    else
        mSocket.async_shutdown (boost::bind (&dummy_handler));
}

std::string RPCServer::handleRequest (const std::string& requestStr)
{
    WriteLog (lsTRACE, RPCServer) << "handleRequest " << requestStr;

    Json::Value id;

    // Parse request
    Json::Value     jvRequest;
    Json::Reader    reader;

    if (!reader.parse (requestStr, jvRequest) || jvRequest.isNull () || !jvRequest.isObject ())
        return (HTTPReply (400, "unable to parse request"));

    // Parse id now so errors from here on will have the id
    id = jvRequest["id"];

    // Parse method
    Json::Value valMethod = jvRequest["method"];

    if (valMethod.isNull ())
        return (HTTPReply (400, "null method"));

    if (!valMethod.isString ())
        return (HTTPReply (400, "method is not string"));

    std::string strMethod = valMethod.asString ();

    // Parse params
    Json::Value valParams = jvRequest["params"];

    if (valParams.isNull ())
    {
        valParams = Json::Value (Json::arrayValue);
    }
    else if (!valParams.isArray ())
    {
        return HTTPReply (400, "params unparseable");
    }

    try
    {
        mRole   = iAdminGet (jvRequest, mSocket.PlainSocket ().remote_endpoint ().address ().to_string ());
    }
    catch (...)
    {
        // endpoint already disconnected
        return "";
    }

    if (RPCHandler::FORBID == mRole)
    {
        // XXX This needs rate limiting to prevent brute forcing password.
        return HTTPReply (403, "Forbidden");
    }

    RPCHandler mRPCHandler (mNetOps);

    WriteLog (lsTRACE, RPCServer) << valParams;
    int cost = 10;
    Json::Value result = mRPCHandler.doRpcCommand (strMethod, valParams, mRole, cost);
    WriteLog (lsTRACE, RPCServer) << result;

    std::string strReply = JSONRPCReply (result, Json::Value (), id);
    return HTTPReply (200, strReply);
}

#if 0
// now, expire, n
bool RPCServer::parseAcceptRate (const std::string& sAcceptRate)
{
    if (!sAcceptRate.compare ("expire"))
        0;

    return true;
}
#endif


void RPCServer::handle_write (const boost::system::error_code& e)
{
    //std::cerr << "async_write complete " << e << std::endl;

    if (!e)
    {
        HTTPRequestAction action = mHTTPRequest.requestDone (false);

        if (action == haCLOSE_CONN)
            mSocket.async_shutdown (boost::bind (&dummy_handler));
        else
        {
            boost::asio::async_read_until (mSocket, mLineBuffer, "\r\n",
                                           boost::bind (&RPCServer::handle_read_line, shared_from_this (), boost::asio::placeholders::error));
        }
    }

    if (e != boost::asio::error::operation_aborted)
    {
        //connection_manager_.stop(shared_from_this());
    }
}

// vim:ts=4
