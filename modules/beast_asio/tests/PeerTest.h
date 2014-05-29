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

#ifndef BEAST_ASIO_TESTS_PEERTEST_H_INCLUDED
#define BEAST_ASIO_TESTS_PEERTEST_H_INCLUDED

#include <beast/unit_test/suite.h>

namespace beast {
namespace asio {

/** Performs a test of two peers defined by template parameters. */
class PeerTest
{
public:
    enum
    {
        /** How long to wait before aborting a peer and reporting a timeout.

            @note Aborting synchronous logics may cause undefined behavior.
        */
        defaultTimeoutSeconds = 30
    };

    //--------------------------------------------------------------------------

    /** Holds the test results for one peer.
    */
    class Result
    {
    public:
        /** Default constructor indicates the test was skipped.
        */
        Result ();

        /** Construct from an error code.
            The prefix is prepended to the error message.
        */
        explicit Result (boost::system::error_code const& ec, String const& prefix = "");
        explicit Result (std::exception const& e, String const& prefix = "");

        /** Returns true if the error codes match (message is ignored).
        */
        bool operator== (Result const& other) const noexcept;
        bool operator!= (Result const& other) const noexcept;

        /** Returns true if the peer failed.
        */
        bool failed () const noexcept;

        /** Convenience for determining if the peer timed out.
        */
        bool timedout () const noexcept;

        /** Provides a descriptive message.
            This is suitable to pass to suite::fail.
        */
        String message () const noexcept;

        /** Report the result to a testsuite.
            A return value of true indicates success.
        */
        bool report (unit_test::suite& suite,
            bool reportPassingTests = false) const;

    private:
        boost::system::error_code m_ec;
        String m_message;
    };

    //--------------------------------------------------------------------------

    /** Holds the results for both peers in a test.
    */
    struct Results
    {
        String name;        // A descriptive name for this test case.
        Result client;
        Result server;

        Results ();

        /** Determines if client and server results match. */
        bool operator== (Results const& other) const noexcept;
        bool operator!= (Results const& other) const noexcept;

        /** Report the results to a suite object.
            A return value of true indicates success.
            @param beginTestCase `true` to call test.beginTestCase for you
        */
        bool report (unit_test::suite& suite, bool beginTestCase = true) const;
    };

    //--------------------------------------------------------------------------

    /** Test two peers and return the results.
    */
    template <class Details, class ClientLogic, class ServerLogic,
              class ClientArg, class ServerArg>
    static Results run (ClientArg const& clientArg, ServerArg const& serverArg,
        int timeoutSeconds = defaultTimeoutSeconds)
    {
        Results results;

        if (Process::isRunningUnderDebugger ())
            timeoutSeconds = -1;

        try
        {
            TestPeerType <ServerLogic, Details> server (serverArg);

            results.name = server.name () + Details::getArgName (serverArg);

            try
            {
                TestPeerType <ClientLogic, Details> client (clientArg);

                results.name << " / " + client.name () + Details::getArgName (clientArg);

                try
                {
                    server.start (timeoutSeconds);

                    try
                    {
                        client.start (timeoutSeconds);

                        boost::system::error_code const ec = client.join ();

                        results.client = Result (ec, client.name ());

                        try
                        {
                            boost::system::error_code const ec = server.join ();

                            results.server = Result (ec, server.name ());
                        }
                        catch (std::exception& e)
                        {
                            results.server = Result (e, server.name ());
                        }
                        catch (...)
                        {
                            results.server = Result (TestPeerBasics::make_error (
                                TestPeerBasics::errc::exceptioned), server.name ());
                        }
                    }
                    catch (std::exception& e)
                    {
                        results.server = Result (e, client.name ());
                    }
                    catch (...)
                    {
                        results.client = Result (TestPeerBasics::make_error (
                            TestPeerBasics::errc::exceptioned), client.name ());
                    }
                }
                catch (std::exception& e)
                {
                    results.server = Result (e, server.name ());
                }
                catch (...)
                {
                    results.server = Result (TestPeerBasics::make_error (
                        TestPeerBasics::errc::exceptioned), server.name ());
                }
            }
            catch (std::exception& e)
            {
                results.server = Result (e, "client");
            }
            catch (...)
            {
                results.client = Result (TestPeerBasics::make_error (
                    TestPeerBasics::errc::exceptioned), "client");
            }
        }
        catch (std::exception& e)
        {
            results.server = Result (e, "server");
        }
        catch (...)
        {
            results.server = Result (TestPeerBasics::make_error (
                TestPeerBasics::errc::exceptioned), "server");
        }

        return results;
    }

    template <class Details, class ClientLogic, class ServerLogic, class Arg>
    static Results run (Arg const& arg, int timeoutSeconds = defaultTimeoutSeconds)
    {
        return run <Details, ClientLogic, ServerLogic, Arg, Arg> (
            arg, arg, timeoutSeconds);
    }

    //--------------------------------------------------------------------------

    template <class Details, class Arg>
    static void report_async (unit_test::suite& suite, Arg const& arg,
                              int timeoutSeconds = defaultTimeoutSeconds,
                              bool beginTestCase = true)
    {
        run <Details, TestPeerLogicAsyncClient, TestPeerLogicAsyncServer>
            (arg, timeoutSeconds).report (suite, beginTestCase);
    }

    template <class Details, class Arg>
    static
    void
    report (unit_test::suite& suite, Arg const& arg,
        int timeoutSeconds = defaultTimeoutSeconds, bool beginTestCase = true)
    {
        run <Details, TestPeerLogicSyncClient, TestPeerLogicSyncServer>
            (arg, timeoutSeconds).report (suite, beginTestCase);

        run <Details, TestPeerLogicAsyncClient, TestPeerLogicSyncServer>
            (arg, timeoutSeconds).report (suite, beginTestCase);

        run <Details, TestPeerLogicSyncClient, TestPeerLogicAsyncServer>
            (arg, timeoutSeconds).report (suite, beginTestCase);

        report_async <Details> (suite, arg, timeoutSeconds, beginTestCase);
    }
};

}
}

#endif
