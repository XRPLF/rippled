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

namespace beast {
namespace asio {

TestPeerLogicSyncServer::TestPeerLogicSyncServer (abstract_socket& socket)
    : TestPeerLogic (socket)
{
}

PeerRole TestPeerLogicSyncServer::get_role () const noexcept
{
    return PeerRole::server;
}

TestPeerBasics::Model TestPeerLogicSyncServer::get_model () const noexcept
{
    return Model::sync;
}

void TestPeerLogicSyncServer::on_connect ()
{
    if (socket ().needs_handshake ())
    {
        if (failure (socket ().handshake (to_handshake_type (get_role ()), error ())))
            return;
    }

    {
        boost::asio::streambuf buf (5);
        std::size_t const amount = boost::asio::read_until (
            socket (), buf, "hello", error ());

        if (failure (error ()))
            return;

        if (unexpected (amount == 5, error ()))
            return;

        if (unexpected (buf.size () == 5, error ()))
            return;
    }

    {
        std::size_t const amount = boost::asio::write (
            socket (), boost::asio::buffer ("goodbye", 7), error ());

        if (failure (error ()))
            return;

        if (unexpected (amount == 7, error ()))
            return;
    }

    if (socket ().needs_handshake ())
    {
        if (failure (socket ().shutdown (error ()), true))
            return;
    }

    if (failure (socket ().shutdown (abstract_socket::shutdown_send, error ())))
        return;

    if (failure (socket ().close (error ())))
        return;
}

}
}
