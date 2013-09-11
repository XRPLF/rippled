//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_BASICS_RPCDOOR_H_INCLUDED
#define RIPPLE_NET_BASICS_RPCDOOR_H_INCLUDED

/** Listening socket for RPC requests.
*/
class RPCDoor
{
public:
    static RPCDoor* New (boost::asio::io_service& io_service, RPCServer::Handler& handler);

    virtual ~RPCDoor () { }
};

#endif
