//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PEERDOOR_H_INCLUDED
#define RIPPLE_PEERDOOR_H_INCLUDED

/** Handles incoming connections from peers.
*/
class PeerDoor : LeakChecked <PeerDoor>
{
public:
    virtual ~PeerDoor () { }

    enum Kind
    {
        sslRequired,
        sslAndPROXYRequired
    };

    static PeerDoor* New (Kind kind, std::string const& ip, int port,
        boost::asio::io_service& io_service, boost::asio::ssl::context& ssl_context);

    //virtual boost::asio::ssl::context& getSSLContext () = 0;
};

#endif
