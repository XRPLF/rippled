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

TestPeerLogicAsyncClient::TestPeerLogicAsyncClient (Socket& socket)
    : TestPeerLogic (socket)
{
}

PeerRole TestPeerLogicAsyncClient::get_role () const noexcept
{
    return PeerRole::client;
}

TestPeerBasics::Model TestPeerLogicAsyncClient::get_model () const noexcept
{
    return Model::async;
}

void TestPeerLogicAsyncClient::on_connect_async (error_code const& ec)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    if (socket ().needs_handshake ())
    {
        socket ().async_handshake (Socket::client,
            boost::bind (&TestPeerLogicAsyncClient::on_handshake, this,
            boost::asio::placeholders::error));
    }
    else
    {
        on_handshake (ec);
    }
}

void TestPeerLogicAsyncClient::on_handshake (error_code const& ec)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    boost::asio::async_write (socket (), boost::asio::buffer ("hello", 5),
        boost::bind (&TestPeerLogicAsyncClient::on_write, this,
        boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void TestPeerLogicAsyncClient::on_write (error_code const& ec, std::size_t bytes_transferred)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    if (unexpected (bytes_transferred == 5, error ()))
        return finished ();

    boost::asio::async_read_until (socket (), m_buf, std::string ("goodbye"),
        boost::bind (&TestPeerLogicAsyncClient::on_read, this,
        boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void TestPeerLogicAsyncClient::on_read (error_code const& ec, std::size_t bytes_transferred)
{
    if (aborted (ec) || failure (error (ec)))
        return finished ();

    if (unexpected (bytes_transferred == 7, error ()))
        return finished ();

    // should check the data here?
    m_buf.consume (bytes_transferred);

    // Fire up a 1 byte read, to wait for the server to
    // shut down its end of the connection.
    boost::asio::async_read (socket (), m_buf.prepare (1),
        boost::bind (&TestPeerLogicAsyncClient::on_read_final, this,
        boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void TestPeerLogicAsyncClient::on_read_final (error_code const& ec, std::size_t)
{
    if (aborted (ec))
        return finished ();

    // An eof is the normal case. The server should have closed shop.
    //
    if (ec == boost::asio::error::eof)
    {
        if (socket ().needs_handshake ())
        {
            socket ().async_shutdown (boost::bind (&TestPeerLogicAsyncClient::on_shutdown, this,
                boost::asio::placeholders::error));
        }
        else
        {
            // on_shutdown will call finished ()
            error_code ec;
            on_shutdown (socket ().shutdown (Socket::shutdown_both, ec));
        }
    }
    else
    {
        // If we don't get eof, then there should be some other
        // error in there. We don't expect the server to send more bytes!
        //
        // This statement will do the following:
        //
        // error (ec)     save ec into our error state
        // success ()     return true if ec represents success
        // unexpected ()  changes error() to 'unexpected' result if
        //                success() returned true
        //
        unexpected (success (error (ec)), error ());

        return finished ();
    }
}

void TestPeerLogicAsyncClient::on_shutdown (error_code const& ec)
{
    if (! aborted (ec))
    {
        if (success (error (ec), true))
        {
            if (socket ().needs_handshake ())
            {
                socket ().shutdown (Socket::shutdown_both, error ());
            }

            if (! error ())
            {
                if (success (socket ().close (error ())))
                {
                    // doing nothing here is intended,
                    // as the calls to success() may set error()
                }
            }
        }
    }

    finished ();
}
