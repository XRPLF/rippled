#ifndef __PEERDOOR__
#define __PEERDOOR__

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

/*
Handles incoming connections from other Peers
*/

class PeerDoor
{
public:
    PeerDoor (boost::asio::io_service& io_service);

    boost::asio::ssl::context&  getSSLContext ()
    {
        return mCtx;
    }

private:
    boost::asio::ip::tcp::acceptor  mAcceptor;
    boost::asio::ssl::context       mCtx;
    boost::asio::deadline_timer     mDelayTimer;

    void    startListening ();
    void    handleConnect (Peer::pointer new_connection, const boost::system::error_code& error);
};

#endif

// vim:ts=4
