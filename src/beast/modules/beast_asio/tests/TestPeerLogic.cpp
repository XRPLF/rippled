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

TestPeerLogic::TestPeerLogic (abstract_socket& socket)
    : m_socket (&socket)
{
}

TestPeerLogic::error_code& TestPeerLogic::error () noexcept
{
    return m_ec;
}

TestPeerLogic::error_code const& TestPeerLogic::error () const noexcept
{
    return m_ec;
}

TestPeerLogic::error_code const& TestPeerLogic::error (error_code const& ec) noexcept
{
    return m_ec = ec;
}

abstract_socket& TestPeerLogic::socket () noexcept
{
    return *m_socket;
}

void TestPeerLogic::on_connect ()
{
    pure_virtual ();
}

void TestPeerLogic::on_connect_async (error_code const&)
{
    pure_virtual ();
}

void TestPeerLogic::finished ()
{
    pure_virtual ();
}

void TestPeerLogic::pure_virtual ()
{
    fatal_error ("A TestPeerLogic function was called incorrectly");
}

}
}
