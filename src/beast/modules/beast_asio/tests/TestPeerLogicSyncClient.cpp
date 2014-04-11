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

TestPeerLogicSyncClient::TestPeerLogicSyncClient (abstract_socket& socket)
    : TestPeerLogic (socket)
{
}

PeerRole TestPeerLogicSyncClient::get_role () const noexcept
{
    return PeerRole::client;
}

TestPeerBasics::Model TestPeerLogicSyncClient::get_model () const noexcept
{
    return Model::sync;
}

void TestPeerLogicSyncClient::on_connect ()
{
    {
        // pre-handshake hook is optional
        on_pre_handshake ();
        if (failure (error ()))
            return ;
    }

    if (socket ().needs_handshake ())
    {
        if (failure (socket ().handshake (to_handshake_type (get_role ()), error ())))
            return;
    }

    {
        std::size_t const amount = boost::asio::write (
            socket (), boost::asio::buffer ("hello", 5), error ());

        if (failure (error ()))
            return;

        if (unexpected (amount == 5, error ()))
            return;
    }

    {
        char data [7];

        size_t const amount = boost::asio::read (
            socket (), boost::asio::buffer (data, 7), error ());

        if (failure (error ()))
            return;

        if (unexpected (amount == 7, error ()))
            return;

        if (unexpected (memcmp (&data, "goodbye", 7) == 0, error ()))
            return;
    }

    // Wait for 1 byte which should never come. Instead,
    // the server should close its end and we will get eof
    {
        char data [1];
        boost::asio::read (socket (), boost::asio::buffer (data, 1), error ());

        if (error () == boost::asio::error::eof)
        {
            error () = error_code ();
        }
        else if (unexpected (failure (error ()), error ()))
        {
            return;
        }
    }

    if (socket ().needs_handshake ())
    {
        if (failure (socket ().shutdown (error ()), true))
            return;
        error () = error_code ();
    }

    if (failure (socket ().shutdown (abstract_socket::shutdown_send, error ())))
        return;

    if (failure (socket ().close (error ())))
        return;
}

void TestPeerLogicSyncClient::on_pre_handshake ()
{
}
}

}
