//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <BeastConfig.h>

#include <ripple/basics/Log.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/core/ReportUncaughtException.h>

namespace ripple {

// reportUncaughtException is disabled if NO_LOG_UNHANDLED_EXCEPTIONS is defined.
#ifndef NO_LOG_UNHANDLED_EXCEPTIONS

class ReportUncaughtException_test : public TestSuite
{
public:
    class TestSink : public beast::Journal::Sink
    {
        std::stringstream ss_;

    public:
        TestSink()
            : Sink (beast::severities::kFatal, false)
        {
        }

        std::string getText() const
        {
            return ss_.str();
        }

        void reset()
        {
            ss_.str("");
        }

        void
        write (beast::severities::Severity level, std::string const& s) override
        {
            if (level >= threshold())
                ss_ << s;
        }
    };

    class ExceptionGen
    {
    public:
        // A place to keep methods that throw.
        void dontThrow()
        {
            return;
        }

        void throwStdExcept()
        {
            throw std::logic_error ("logic_error");
        }

        void throwForcedUnwind()
        {
            throw boost::coroutines::detail::forced_unwind ();
        }

        void throwWeird()
        {
            throw std::string ("Pretty unusual...");
        }
    };

    void test ()
    {
        // Install our own debug Sink so we can see what gets written.
        // Retain the old Sink so we can put it back.
        auto testSink = std::make_unique<TestSink>();
        TestSink& sinkRef = *testSink;
        std::unique_ptr<beast::Journal::Sink> oldSink =
            setDebugLogSink (std::move (testSink));

        ExceptionGen exGen;

        // Make sure nothing gets logged if there's no exception.
        reportUncaughtException (&exGen, &ExceptionGen::dontThrow, "noThrow");
        expect (sinkRef.getText() == "");
        sinkRef.reset();

        reportUncaughtException (&exGen, &ExceptionGen::dontThrow, "noThrow",
            [] { return "Just noise"; });

        expect (sinkRef.getText() == "");
        sinkRef.reset();

        using PExGenMemFn = void (ExceptionGen::*) ();
        auto testCase = [this, &exGen, &sinkRef] (
            PExGenMemFn call, char const* description)
            {
                std::string want = std::string ("Unhandled exception in "
                    "testFn; Exception: ") + description;

                // Test the case without the closing lambda.
                bool gotException = false;
                try
                {
                    reportUncaughtException (&exGen, call, "testFn");
                }
                catch (...)
                {
                    gotException = true;
                }
                expect (gotException == true);
                expect (sinkRef.getText() == want);
                sinkRef.reset();

                // Try the case with the closing lambda.
                gotException = false;
                try
                {
                    reportUncaughtException (&exGen, call, "testFn",
                        []{ return "extra info"; });
                }
                catch (...)
                {
                    gotException = true;
                }
                expect (gotException == true);
                expect (sinkRef.getText() == want + "; extra info");
                sinkRef.reset();
            };

        // Test logging for a stad::exception.
        testCase (&ExceptionGen::throwStdExcept, "logic_error");

        // Test logging for a forced_unwind.
        testCase (&ExceptionGen::throwForcedUnwind, "forced_unwind");

        // Test logging for none of the above.
        testCase (&ExceptionGen::throwWeird, "unknown exception type");

        // We're done with TestSink.  Re-install the old Sink.
        setDebugLogSink (std::move (oldSink));
    }

    void run ()
    {
        test ();
    }
};

BEAST_DEFINE_TESTSUITE (ReportUncaughtException, core, ripple);

#endif // NO_LOG_UNHANDLED_EXCEPTIONS

}  // ripple
