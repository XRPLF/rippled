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

#ifndef BEAST_ASIO_TESTS_TESTPEERLOGICASYNCCLIENT_H_INCLUDED
#define BEAST_ASIO_TESTS_TESTPEERLOGICASYNCCLIENT_H_INCLUDED

class TestPeerLogicAsyncClient : public TestPeerLogic
{
public:
    explicit TestPeerLogicAsyncClient (Socket& socket);
    PeerRole get_role () const noexcept;
    Model get_model () const noexcept;
    void on_connect_async (error_code const& ec);
    void on_handshake (error_code const& ec);
    void on_write (error_code const& ec, std::size_t bytes_transferred);
    void on_read (error_code const& ec, std::size_t bytes_transferred);
    void on_read_final (error_code const& ec, std::size_t);
    void on_shutdown (error_code const& ec);
private:
    boost::asio::streambuf m_buf;
};

#endif
