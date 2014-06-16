//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/asio/placeholders.h>

namespace beast {
namespace asio {

TestPeerLogicAsyncServer::TestPeerLogicAsyncServer (abstract_socket& socket)
    : TestPeerLogic (socket)
{
}

PeerRole TestPeerLogicAsyncServer::get_role () const noexcept
{
    return PeerRole::server;
}

TestPeerBasics::Model TestPeerLogicAsyncServer::get_model () const noexcept
{
    return Model::async;
}

void TestPeerLogicAsyncServer::on_connect_async (error_code const& ec)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    if (socket ().needs_handshake ())
    {
        socket ().async_handshake (abstract_socket::server,
            std::bind (&TestPeerLogicAsyncServer::on_handshake, this,
                beast::asio::placeholders::error));
    }
    else
    {
        on_handshake (ec);
    }
}

void TestPeerLogicAsyncServer::on_handshake (error_code const& ec)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    boost::asio::async_read_until (socket (), m_buf, std::string ("hello"),
        std::bind (&TestPeerLogicAsyncServer::on_read, this,
            beast::asio::placeholders::error, beast::asio::placeholders::bytes_transferred));
}

void TestPeerLogicAsyncServer::on_read (error_code const& ec, std::size_t bytes_transferred)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    if (unexpected (bytes_transferred == 5, error ()))
        return finished ();

    boost::asio::async_write (socket (), boost::asio::buffer ("goodbye", 7),
        std::bind (&TestPeerLogicAsyncServer::on_write, this,
            beast::asio::placeholders::error, beast::asio::placeholders::bytes_transferred));
}

void TestPeerLogicAsyncServer::on_write (error_code const& ec, std::size_t bytes_transferred)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    if (unexpected (bytes_transferred == 7, error ()))
        return finished ();

    if (socket ().needs_handshake ())
    {
        socket ().async_shutdown (std::bind (&TestPeerLogicAsyncServer::on_shutdown, this,
            beast::asio::placeholders::error));
    }
    else
    {
        // on_shutdown will call finished ()
        // we need another instance of ec so we can call on_shutdown()
        error_code ec;
        on_shutdown (socket ().shutdown (abstract_socket::shutdown_receive, ec));
    }
}

void TestPeerLogicAsyncServer::on_shutdown (error_code const& ec)
{
    if (! aborted (ec))
    {
        if (success (error (ec), true))
        {
            if (socket ().needs_handshake ())
            {
                socket ().shutdown (abstract_socket::shutdown_receive, error ());
            }

            if (success (socket ().close (error ())))
            {
                // doing nothing here is intended,
                // as the calls to success() may set error()
            }
        }
    }

    finished ();
}

}
}

