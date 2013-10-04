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


SETUP_LOG (PeerDoor)

class PeerDoorImp
    : public PeerDoor
    , public LeakChecked <PeerDoorImp>
{
public:
    PeerDoorImp (Stoppable& parent, Kind kind, std::string const& ip, int port,
        boost::asio::io_service& io_service, boost::asio::ssl::context& ssl_context)
        : PeerDoor (parent)
        , m_kind (kind)
        , m_ssl_context (ssl_context)
        , mAcceptor (io_service, boost::asio::ip::tcp::endpoint (
            boost::asio::ip::address ().from_string (ip.empty () ? "0.0.0.0" : ip), port))
        , mDelayTimer (io_service)
    {
        if (! ip.empty () && port != 0)
        {
            Log (lsINFO) << "Peer port: " << ip << " " << port;
            
            async_accept ();
        }
    }

    ~PeerDoorImp ()
    {
    }

    //--------------------------------------------------------------------------

    // Initiating function for performing an asynchronous accept
    //
    void async_accept ()
    {
        bool const isInbound (true);
        bool const requirePROXYHandshake (m_kind == sslAndPROXYRequired);

        Peer::pointer new_connection (Peer::New (
            mAcceptor.get_io_service (), m_ssl_context,
                getApp().getPeers ().assignPeerId (),
                    isInbound, requirePROXYHandshake));

        mAcceptor.async_accept (new_connection->getNativeSocket (),
            boost::bind (&PeerDoorImp::handleAccept, this,
                boost::asio::placeholders::error,
                new_connection));
    }

    //--------------------------------------------------------------------------

    // Called when the deadline timer wait completes
    //
    void handleTimer (boost::system::error_code ec)
    {
        async_accept ();
    }

    // Called when the accept socket wait completes
    //
    void handleAccept (boost::system::error_code ec, Peer::pointer new_connection)
    {
        bool delay = false;

        if (! ec)
        {
            // VFALCO NOTE the error code doesnt seem to be used in connected()
            new_connection->connected (ec);
        }
        else
        {
            if (ec == boost::system::errc::too_many_files_open)
                delay = true;
            WriteLog (lsERROR, PeerDoor) << ec;
        }

        if (delay)
        {
            mDelayTimer.expires_from_now (boost::posix_time::milliseconds (500));
            mDelayTimer.async_wait (boost::bind (&PeerDoorImp::handleTimer,
                this, boost::asio::placeholders::error));
        }
        else
        {
            async_accept ();
        }
    }

    //--------------------------------------------------------------------------

    void onStop ()
    {
        {
            boost::system::error_code ec;
            mDelayTimer.cancel (ec);
        }

        {
            boost::system::error_code ec;
            mAcceptor.cancel (ec);
        }

        stopped ();
    }

private:
    Kind m_kind;
    boost::asio::ssl::context& m_ssl_context;
    boost::asio::ip::tcp::acceptor  mAcceptor;
    boost::asio::deadline_timer     mDelayTimer;
};

//------------------------------------------------------------------------------

PeerDoor::PeerDoor (Stoppable& parent)
    : AsyncService ("PeerDoor", parent)
{
}

//------------------------------------------------------------------------------

PeerDoor* PeerDoor::New (Stoppable& parent, Kind kind, std::string const& ip, int port,
    boost::asio::io_service& io_service, boost::asio::ssl::context& ssl_context)
{
    return new PeerDoorImp (parent, kind, ip, port, io_service, ssl_context);
}
