//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RPCSERVER_H_INCLUDED
#define RIPPLE_RPCSERVER_H_INCLUDED

// VFALCO NOTE This looks like intrusve shared object?
//
class RPCServer
    : public boost::enable_shared_from_this <RPCServer>
    , LeakChecked <RPCServer>
{
public:
    typedef boost::shared_ptr <RPCServer> pointer;

public:
    static pointer New (
        boost::asio::io_service& io_service,
        boost::asio::ssl::context& context,
        NetworkOPs* mNetOps);

    virtual AutoSocket& getSocket () = 0;

    // VFALCO TODO Remove this since it exposes boost
    virtual boost::asio::ip::tcp::socket& getRawSocket () = 0;

    virtual void connected () = 0;
};

#endif
