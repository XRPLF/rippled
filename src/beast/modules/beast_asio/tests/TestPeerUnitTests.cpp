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

/** UnitTest for the TestPeer family of objects. */
class TestPeerUnitTests : public UnitTest
{
public:

    template <typename Details, typename Arg >
    void testDetails (Arg const& arg = Arg ())
    {
        PeerTest::report <Details> (*this, arg, timeoutSeconds);
    }

    void runTest ()
    {
        typedef boost::asio::ip::tcp protocol;
        testDetails <TcpDetails, TcpDetails::arg_type> (protocol::v4 ());
        testDetails <TcpDetails, TcpDetails::arg_type> (protocol::v6 ());
    }

    //--------------------------------------------------------------------------

    enum
    {
        timeoutSeconds = 10
    };

    TestPeerUnitTests () : UnitTest ("TestPeer", "beast")
    {
    }
};

static TestPeerUnitTests testPeerUnitTests;

}
}
