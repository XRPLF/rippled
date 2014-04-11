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

#ifndef BEAST_ASIO_TESTS_TESTPEER_H_INCLUDED
#define BEAST_ASIO_TESTS_TESTPEER_H_INCLUDED

namespace beast {
namespace asio {

/** An abstract peer for unit tests. */
class TestPeer : public TestPeerBasics
{
public:
    enum
    {
        // This should be long enough to go about your business.
        defaultTimeout = 10
    };

    virtual ~TestPeer () { }

    /** Get the name of this peer. */
    virtual String name () const = 0;

    /** Start the peer.
        If timeoutSeconds is 0 or less, the wait is infinite.
        @param timeoutSeconds How long until the peer should be
                              considered timed out.
    */
    virtual void start (int timeoutSeconds = defaultTimeout) = 0;

    /** Wait for the peer to finish.

        @return Any error code generated during the server operation.
    */
    virtual boost::system::error_code join () = 0;
};

}
}

#endif
