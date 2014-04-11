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

#include "PeerDoor.h"

namespace ripple {

SETUP_LOG (PeerDoor)

class PeerDoorImp
    : public PeerDoor
    , public LeakChecked <PeerDoorImp>
{
public:
    PeerDoorImp (Kind kind, Peers& peers,
                 boost::asio::ip::tcp::endpoint const &ep,
                 boost::asio::io_service& io_service)
        : PeerDoor (static_cast<Stoppable&>(peers))
        , m_peers (peers)
        , m_journal (LogPartition::getJournal <PeerDoor> ())
        , m_kind (kind)
        , m_acceptor (io_service, ep)
        , m_acceptDelay (io_service)
    {
        m_journal.info <<
            "Listening on " <<
            IPAddressConversion::from_asio (
                m_acceptor.local_endpoint()) <<
            ((m_kind == sslAndPROXYRequired) ? " (proxy)" : "");

        async_accept ();
    }

    ~PeerDoorImp ()
    {
    }

    //--------------------------------------------------------------------------

    // Initiating function for performing an asynchronous accept
    //
    void async_accept ()
    {
        boost::shared_ptr <NativeSocketType> socket (
            boost::make_shared <NativeSocketType> (
                m_acceptor.get_io_service()));

        m_acceptor.async_accept (*socket,
            boost::bind (&PeerDoorImp::handleAccept, this,
                boost::asio::placeholders::error,
                    socket));
    }

    //--------------------------------------------------------------------------

    // Called when the deadline timer wait completes
    //
    void handleTimer (boost::system::error_code ec)
    {
        if (ec == boost::asio::error::operation_aborted || isStopping ())
            return;

        async_accept ();
    }

    // Called when the accept socket wait completes
    //
    void handleAccept (boost::system::error_code ec,
        boost::shared_ptr <NativeSocketType> const& socket)
    {
        if (ec == boost::asio::error::operation_aborted || isStopping ())
            return;

        bool delay = false;

        if (! ec)
        {
            bool const proxyHandshake (m_kind == sslAndPROXYRequired);

            m_peers.accept (proxyHandshake, socket);
        }
        else
        {
            if (ec == boost::system::errc::too_many_files_open)
                delay = true;

            m_journal.info << "Error " << ec;
        }

        if (delay)
        {
            m_acceptDelay.expires_from_now (boost::posix_time::milliseconds (500));
            m_acceptDelay.async_wait (boost::bind (&PeerDoorImp::handleTimer,
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
            m_acceptDelay.cancel (ec);
        }

        {
            boost::system::error_code ec;
            m_acceptor.cancel (ec);
        }

        stopped ();
    }

private:
    Peers& m_peers;
    Journal m_journal;
    Kind m_kind;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::deadline_timer m_acceptDelay;
};

//------------------------------------------------------------------------------

PeerDoor::PeerDoor (Stoppable& parent)
    : Stoppable ("PeerDoor", parent)
{
}

//------------------------------------------------------------------------------
PeerDoor* PeerDoor::New (
    Kind kind, Peers& peers,
        std::string const& ip, int port,
        boost::asio::io_service& io_service)
{
    // You have to listen on something!
    bassert(port != 0);

    boost::asio::ip::tcp::endpoint ep(
        boost::asio::ip::address ().from_string (
            ip.empty () ? "0.0.0.0" : ip), port);

    return new PeerDoorImp (kind, peers, ep, io_service);
}

}
