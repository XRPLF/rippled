#ifndef __RPCSERVER__
#define __RPCSERVER__

#include "HTTPRequest.h"
#include "RPCHandler.h"
#include "LoadManager.h"

class RPCServer : public boost::enable_shared_from_this<RPCServer>
{
public:

    typedef boost::shared_ptr<RPCServer> pointer;

private:

    NetworkOPs* mNetOps;

    AutoSocket mSocket;
    boost::asio::io_service::strand mStrand;

    boost::asio::streambuf mLineBuffer;
    Blob mQueryVec;
    std::string mReplyStr;

    HTTPRequest mHTTPRequest;


    int mRole;

    RPCServer (boost::asio::io_service& io_service, boost::asio::ssl::context& ssl_context, NetworkOPs* nopNetwork);

    RPCServer (const RPCServer&); // no implementation
    RPCServer& operator= (const RPCServer&); // no implementation

    void handle_write (const boost::system::error_code& ec);
    void handle_read_line (const boost::system::error_code& ec);
    void handle_read_req (const boost::system::error_code& ec);
    void handle_shutdown (const boost::system::error_code& ec);

    std::string handleRequest (const std::string& requestStr);

public:
    static pointer create (boost::asio::io_service& io_service, boost::asio::ssl::context& context, NetworkOPs* mNetOps)
    {
        return pointer (new RPCServer (io_service, context, mNetOps));
    }

    AutoSocket& getSocket ()
    {
        return mSocket;
    }

    boost::asio::ip::tcp::socket& getRawSocket ()
    {
        return mSocket.PlainSocket ();
    }

    void connected ();
};

#endif

// vim:ts=4
