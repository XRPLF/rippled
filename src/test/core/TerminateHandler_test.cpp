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

#include <ripple/core/TerminateHandler.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/beast/unit_test.h>

#include <boost/coroutine/exceptions.hpp>
#include <exception>
#include <sstream>
#include <streambuf>

namespace ripple {
namespace test {

class TerminateHandler_test : public beast::unit_test::suite
{
private:
    // Allow cerr to be redirected.  Destructor restores old cerr streambuf.
    class CerrRedirect
    {
    public:
        CerrRedirect (std::stringstream& sStream)
        : old_ (std::cerr.rdbuf (sStream.rdbuf()))
        { }

        ~CerrRedirect()
        {
            std::cerr.rdbuf (old_);
        }

    private:
        std::streambuf* const old_;
    };

    // Set a new current thread name.  Destructor restores the old thread name.
    class ThreadNameGuard
    {
    public:
        ThreadNameGuard (std::string const& newName)
        : old_ (beast::getCurrentThreadName ())
        {
            beast::setCurrentThreadName (newName);
        }

        ~ThreadNameGuard()
        {
            std::string oldName;
            if (old_)
                oldName = std::move (*old_);

            beast::setCurrentThreadName (oldName);
        }

    private:
        boost::optional<std::string> old_;
    };

public:
    void
    run() override
    {
        // Set the current thread name, but restore the old name on exit.
        std::string const threadName {"terminateHandler_test"};
        ThreadNameGuard nameGuard {threadName};
        {
            // Test terminateHandler() with a std::exception.

            // The terminateHandler() output goes to std::cerr.  Capture that.
            std::stringstream cerrCapture;
            CerrRedirect cerrRedirect {cerrCapture};

            try
            {
                throw std::range_error ("Out of range");
            }
            catch (...)
            {
                terminateHandler();
            }
            {
                std::string result = cerrCapture.str();
                BEAST_EXPECT (result.find (threadName) != std::string::npos);
                BEAST_EXPECT (
                    result.find ("Out of range") != std::string::npos);
            }
        }

        {
            // Verify terminateHnadler() handles forced_unwind correctly.
            std::stringstream cerrCapture;
            CerrRedirect cerrRedirect {cerrCapture};

            try
            {
                throw boost::coroutines::detail::forced_unwind();
            }
            catch (...)
            {
                terminateHandler();
            }
            {
                std::string result = cerrCapture.str();
                BEAST_EXPECT (result.find (threadName) != std::string::npos);
                BEAST_EXPECT (
                    result.find ("forced_unwind") != std::string::npos);
            }
        }

        {
            // Verify terminatHandler()'s handling of non-standard exceptions.
            std::stringstream cerrCapture;
            CerrRedirect cerrRedirect {cerrCapture};

            try
            {
                throw 7;
            }
            catch (...)
            {
                terminateHandler();
            }
            {
                std::string result = cerrCapture.str();
                BEAST_EXPECT (result.find (threadName) != std::string::npos);
                BEAST_EXPECT (
                    result.find ("unknown exception") != std::string::npos);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(TerminateHandler,core,ripple);

} // test
} // ripple
