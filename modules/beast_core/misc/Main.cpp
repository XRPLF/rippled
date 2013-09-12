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

Main* Main::s_instance;

Main::Main ()
{
    // If this happens it means there are two instances of Main!
    check_precondition (s_instance == nullptr);

    s_instance = this;

}

Main::~Main ()
{
    s_instance = nullptr;
}

Main& Main::getInstance ()
{
    bassert (s_instance != nullptr);

    return *s_instance;
}

int Main::runStartupUnitTests ()
{
    int exitCode = EXIT_SUCCESS;

    struct StartupUnitTests : UnitTests
    {
        void logMessage (String const&)
        {
            // Intentionally do nothing, we don't want
            // to see the extra output for startup tests.
        }

        void log (String const& message)
        {
#if BEAST_MSVC
            if (beast_isRunningUnderDebugger ())
                Logger::outputDebugString (message);
#endif

            std::cerr << message.toStdString () << std::endl;
        }

        void reportCase (String const& suiteName, UnitTest::Case const& testcase)
        {
            String s;
            s << suiteName << " (" << testcase.name << ") produced " <<
                String (testcase.failures) <<
                ((testcase.failures == 1) ? " failure." : " failures.");
            log (s);
        }

        void reportSuite (UnitTest::Suite const& suite)
        {
            if (suite.failures > 0)
            {
                String const suiteName = suite.getSuiteName ();

                for (int i = 0; i < suite.cases.size (); ++i)
                {
                    UnitTest::Case const& testcase (*suite.cases [i]);

                    if (testcase.failures > 0)
                        reportCase (suiteName, testcase);
                }
            }
        }

        void reportSuites (UnitTests::Results const& results)
        {
            for (int i = 0; i < results.suites.size (); ++i)
                reportSuite (*results.suites [i]);
        }

        void reportResults ()
        {
            reportSuites (getResults ());
        }
    };

    StartupUnitTests tests;

    tests.runTests (tests.selectStartupTests ());

    if (tests.anyTestsFailed ())
    {
        tests.reportResults  ();

        tests.log ("Terminating with an error due to failed startup tests.");

        exitCode = EXIT_FAILURE;
    }

    return exitCode;
}

int Main::runFromMain (int argc, char const* const* argv)
{
    int exitCode (runStartupUnitTests ());

    if (exitCode == EXIT_SUCCESS)
        exitCode = run (argc, argv);

    return exitCode;
}
