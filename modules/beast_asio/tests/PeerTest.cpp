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

PeerTest::Result::Result ()
    : m_ec (TestPeerBasics::make_error (TestPeerBasics::errc::skipped))
    , m_message (m_ec.message ())
{
}

PeerTest::Result::Result (boost::system::error_code const& ec, String const& prefix)
    : m_ec (ec)
    , m_message ((prefix == String::empty) ? ec.message ()
                : prefix + ": " + ec.message ())
{
}

PeerTest::Result::Result (std::exception const& e, String const& prefix)
    : m_ec (TestPeerBasics::make_error (TestPeerBasics::errc::exceptioned))
    , m_message ((prefix == String::empty) ? e.what ()
                : prefix + ": " + e.what ())
{
}

bool PeerTest::Result::operator== (Result const& other) const noexcept
{
    return m_ec == other.m_ec;
}

bool PeerTest::Result::operator!= (Result const& other) const noexcept
{
    return m_ec != other.m_ec;
}

bool PeerTest::Result::failed () const noexcept
{
    return TestPeerBasics::failure (m_ec);
}

bool PeerTest::Result::timedout () const noexcept
{
    return m_ec == TestPeerBasics::make_error (TestPeerBasics::errc::timeout);
}

String PeerTest::Result::message () const noexcept
{
    return m_message;
}

bool PeerTest::Result::report (unit_test::suite& suite,
    bool reportPassingTests) const
{
    bool const success = suite.expect (! failed (),
        message ().toStdString());
    if (reportPassingTests && success)
        suite.log <<
            "pass " + message().toStdString();
    return success;
}

//------------------------------------------------------------------------------

PeerTest::Results::Results ()
    : name ("unknown")
{
}

bool PeerTest::Results::operator== (Results const& other) const noexcept
{
    return (client == other.client) && (server == other.server);
}

bool PeerTest::Results::operator!= (Results const& other) const noexcept
{
    return (client != other.client) || (server != other.server);
}

bool PeerTest::Results::report (unit_test::suite& suite,
        bool beginTestCase) const
{
    if (beginTestCase)
        suite.testcase (name.toStdString());
    bool success = true;
    if (! client.report (suite))
        success = false;
    if (! server.report (suite))
        success = false;
    return success;
}

}
}
