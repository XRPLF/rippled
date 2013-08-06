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

    static PeerDoor* New (
        std::string const& ip,
        int port,
        std::string const& sslCiphers,
        boost::asio::io_service& io_service);

    virtual boost::asio::ssl::context& getSSLContext () = 0;
};

#endif
