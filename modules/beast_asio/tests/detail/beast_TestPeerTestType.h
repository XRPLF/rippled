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

#ifndef RIPPLE_TESTPEERTESTTYPE_H_INCLUDED
#define RIPPLE_TESTPEERTESTTYPE_H_INCLUDED

/** Performs a test of two peers defined by template parameters.
*/
class TestPeerTestType : public TestPeerTest
{
public:
    /** Test two peers and return the results.
    */
    template <typename Details, typename ServerLogic, typename ClientLogic, class Arg>
    static Results run (Arg const& arg, int timeoutSeconds = defaultTimeoutSeconds)
    {
        Results results;

        results.name = Details::getArgName (arg);

        try
        {
            TestPeerType <ServerLogic, Details> server (arg);

            results.name << " / " << server.name ();

            try
            {
                TestPeerType <ClientLogic, Details> client (arg);

                results.name << " / " << client.name ();

                try
                {
                    server.start ();

                    try
                    {
                        client.start ();

                        boost::system::error_code const ec =
                            client.join (timeoutSeconds);

                        results.client = Result (ec, client.name ());

                        try
                        {
                            boost::system::error_code const ec =
                                server.join (timeoutSeconds);

                            results.server = Result (ec, server.name ());

                        }
                        catch (...)
                        {
                            results.server = Result (make_error (
                                errc::exceptioned), server.name ());
                        }
                    }
                    catch (...)
                    {
                        results.client = Result (make_error (
                            errc::exceptioned), client.name ());
                    }
                }
                catch (...)
                {
                    results.server = Result (make_error (
                        errc::exceptioned), server.name ());
                }
            }
            catch (...)
            {
                results.client = Result (make_error (
                    errc::exceptioned), "client");
            }
        }
        catch (...)
        {
            results.server = Result (make_error (
                errc::exceptioned), "server");
        }

        return results;
    }

    //--------------------------------------------------------------------------

    /** Reports tests of Details against all known logic combinations to a UnitTest.
    */
    template <typename Details, class Arg>
    static void test (UnitTest& test, Arg const& arg,
                      int timeoutSeconds = defaultTimeoutSeconds,
                      bool beginTestCase = true)
    {
        run <Details, TestPeerLogicSyncServer,  TestPeerLogicSyncClient>  (arg, timeoutSeconds).report (test, beginTestCase);
        run <Details, TestPeerLogicSyncServer,  TestPeerLogicAsyncClient> (arg, timeoutSeconds).report (test, beginTestCase);
        run <Details, TestPeerLogicAsyncServer, TestPeerLogicSyncClient>  (arg, timeoutSeconds).report (test, beginTestCase);
        run <Details, TestPeerLogicAsyncServer, TestPeerLogicAsyncClient> (arg, timeoutSeconds).report (test, beginTestCase);
    }
};

#endif
