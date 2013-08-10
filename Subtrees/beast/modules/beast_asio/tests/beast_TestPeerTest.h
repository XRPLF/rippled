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

#ifndef RIPPLE_TESTPEERTEST_H_INCLUDED
#define RIPPLE_TESTPEERTEST_H_INCLUDED

/** Performs a test of two peers defined by template parameters.
*/
struct TestPeerTest : protected TestPeerBasics
{
    enum
    {
        /** How long to wait before aborting a peer and reporting a timeout.

            @note Aborting synchronous logics may cause undefined behavior.
        */
        defaultTimeoutSeconds = 30
    };

    /** Holds the results for one peer. */
    class Result : protected TestPeerBasics
    {
    public:
        /** Default constructor indicates the test was skipped.
        */
        Result ()
            : m_ec (make_error (errc::skipped))
            , m_message (m_ec.message ())
        {
        }

        /** Construct from an error code.
            The prefix is prepended to the error message.
        */
        explicit Result (boost::system::error_code const& ec, String const& prefix = "")
            : m_ec (ec)
            , m_message ((prefix == String::empty) ? ec.message ()
                        : prefix + " " + ec.message ())
        {
        }

        /** Returns true if the peer failed.
        */
        bool failed () const noexcept
        {
            return failure (m_ec);
        }

        /** Convenience for determining if the peer timed out. */
        bool timedout () const noexcept
        {
            return m_ec == make_error (errc::timeout);
        }

        /** Provides a descriptive message.
            This is suitable to pass to UnitTest::fail.
        */
        String message () const noexcept
        {
            return m_message;
        }

        /** Report the result to a UnitTest object.
            A return value of true indicates success.
        */
        bool report (UnitTest& test)
        {
            bool const success = test.unexpected (failed (), message ());
#if 0
            // Option to report passing tests
            if (success)
                test.logMessage (String ("passed ") + message());
#endif
            return success;
        }

    private:
        boost::system::error_code m_ec;
        String m_message;
    };

    //--------------------------------------------------------------------------

    /** Holds the results for both peers. */
    struct Results
    {
        String name;        // A descriptive name for this test case.
        Result client;
        Result server;

        Results () : name ("unknown")
        {
        }

        /** Report the results to a UnitTest object.
            A return value of true indicates success.
        */
        bool report (UnitTest& test, bool beginTestCase = true)
        {
            if (beginTestCase)
                test.beginTestCase (name);
            bool success = true;
            if (! client.report (test))
                success = false;
            if (! server.report (test))
                success = false;
            return success;
        }
    };
};

#endif
