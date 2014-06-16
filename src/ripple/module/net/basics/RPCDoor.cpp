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

#include <ripple/common/RippleSSLContext.h>

namespace ripple {

SETUP_LOG (RPCDoor)

class RPCDoorImp : public RPCDoor, public beast::LeakChecked <RPCDoorImp>
{
public:
    RPCDoorImp (boost::asio::io_service& io_service, RPCServer::Handler& handler)
        : m_rpcServerHandler (handler)
        , mAcceptor (io_service,
                     boost::asio::ip::tcp::endpoint (boost::asio::ip::address::from_string (getConfig ().getRpcIP ()), getConfig ().getRpcPort ()))
        , mDelayTimer (io_service)
        , m_sslContext ((getConfig ().RPC_SECURE == 0) ?
                RippleSSLContext::createBare () :
                RippleSSLContext::createAuthenticated (
                    getConfig ().RPC_SSL_KEY,
                    getConfig ().RPC_SSL_CERT,
                    getConfig ().RPC_SSL_CHAIN))
    {
        WriteLog (lsINFO, RPCDoor) << "RPC port: " << getConfig ().getRpcAddress().toRawUTF8() << " allow remote: " << getConfig ().RPC_ALLOW_REMOTE;

        startListening ();
    }

    //--------------------------------------------------------------------------

    ~RPCDoorImp ()
    {
        WriteLog (lsINFO, RPCDoor) <<
            "RPC port: " << getConfig ().getRpcAddress().toRawUTF8() <<
            " allow remote: " << getConfig ().RPC_ALLOW_REMOTE;
    }

    //--------------------------------------------------------------------------

    void startListening ()
    {
        RPCServerImp::pointer new_connection (std::make_shared <RPCServerImp> (
            std::ref (mAcceptor.get_io_service ()),
                std::ref (m_sslContext->get ()),
                    std::ref (m_rpcServerHandler)));

        mAcceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

        mAcceptor.async_accept (new_connection->getRawSocket (), 
            new_connection->getRemoteEndpoint (),
            std::bind (&RPCDoorImp::handleConnect, this, 
                new_connection, beast::asio::placeholders::error));
    }

    //--------------------------------------------------------------------------

    bool isClientAllowed (const std::string& ip)
    {
        if (getConfig ().RPC_ALLOW_REMOTE)
            return true;

        // VFALCO TODO Represent ip addresses as a structure. Use isLoopback() member here
        //
        if (ip == "127.0.0.1")
            return true;

        return false;
    }

    //--------------------------------------------------------------------------

    void handleConnect (RPCServerImp::pointer new_connection,
        boost::system::error_code const& error)
    {
        bool delay = false;

        if (!error)
        {
            // Restrict callers by IP
            std::string client_ip (
                new_connection->getRemoteEndpoint ().address ().to_string ());

            if (! isClientAllowed (client_ip))
            {
                startListening ();
                return;
            }

            new_connection->getSocket ().async_handshake (AutoSocket::ssl_socket::server,
                    std::bind (&RPCServer::connected, new_connection));
        }
        else
        {
            if (error == boost::system::errc::too_many_files_open)
                delay = true;

            WriteLog (lsINFO, RPCDoor) << "RPCDoorImp::handleConnect Error: " << error;
        }

        if (delay)
        {
            mDelayTimer.expires_from_now (boost::posix_time::milliseconds (1000));
            mDelayTimer.async_wait (std::bind (&RPCDoorImp::startListening, this));
        }
        else
        {
            startListening ();
        }
    }

private:
    RPCServer::Handler& m_rpcServerHandler;
    boost::asio::ip::tcp::acceptor      mAcceptor;
    boost::asio::deadline_timer         mDelayTimer;
    std::unique_ptr <RippleSSLContext>    m_sslContext;
};

//------------------------------------------------------------------------------

// VFALCO TODO Return std::unique_ptr here
RPCDoor* RPCDoor::New (boost::asio::io_service& io_service, RPCServer::Handler& handler)
{
    return new RPCDoorImp (io_service, handler);
}

}
