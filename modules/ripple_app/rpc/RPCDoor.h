//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RPCDOOR_H
#define RIPPLE_RPCDOOR_H

/** Listening socket for RPC requests.
*/
class RPCDoor
{
public:
    static RPCDoor* New (boost::asio::io_service& io_service, RPCServer::Handler& handler);

    virtual ~RPCDoor () { }
};

#endif
