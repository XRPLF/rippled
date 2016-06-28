//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016, Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_REPORT_UNCAUGHT_EXCEPTION_H_INCLUDED
#define RIPPLE_CORE_REPORT_UNCAUGHT_EXCEPTION_H_INCLUDED

#include <ripple/basics/Log.h>
#include <boost/coroutine/exceptions.hpp> // forced_unwind exception
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace ripple
{
/**
Report uncaught exceptions to DebugLog and cerr

Catch all exceptions that escape the called function.  Report as much
information as can be extracted from the exception to both the DebugLog
and cerr.

The idea is to use this routine at the top of a thread, since on
many platforms the stack trace for an uncaught exception on a thread
is almost useless.

For those platforms where the stack trace from an uncaught exception is
useful (e.g., OS X) this routine is a no-op.  That way a catch will not
interfere with the stack trace showing the real source of the uncaught
exception.

Note that any extra information is passed using a lambda because we only
want to do the work of building the string in the unlikely event of an
uncaught exception.  The lambda is only called in the error case.

Usage example

#include <ripple/core/ReportUncaughtException.h>
#include <chrono>
#include <exception>
#include <thread>

class ThreadedHandler
{
public:
    void operator() ()
    {
        reportUncaughtException (
            this, &ThreadedHandler::runImpl, "ThreadedHandler::operator()");
    }

    void runImpl()
    {
        // do stuff.
        throw std::logic_error("logic_error: What was I thinking?");
    }
};

int main ()
{
    using namespace std::chrono_literals;

    ThreadedHandler handler;
    std::thread  t {handler};
    std::this_thread::sleep_for (1s);
    t.join();
    return 0;
}

@param t Pointer to object to call.
@param threadTop Pointer to member function of t to call.
@param name Name of function to log.
@param lamdba Optional lambda that returns additional text for the log.
*/
template <typename T, typename R, typename L>
void reportUncaughtException (
    T* t, R (T::*threadTop) (), char const* name, L&& lambda)
{
    // Enforce that lambda takes no parameters and returns std::string.
    static_assert (
        std::is_convertible<decltype (lambda()), std::string const>::value,
        "Last argument must be a lamdba taking no arguments "
        "and returning std::string.");

#ifdef NO_LOG_UNHANDLED_EXCEPTIONS
    // Don't use a try block so we can get a good call stack.
    ((t)->*(threadTop)) ();
#else
    // Local lambda for string formatting and re-throwing.
    auto logUncaughtException =
        [name, &lambda] (char const* exName)
        {
            std::stringstream ss;
            ss << "Unhandled exception in " << name
                << "; Exception: " << exName;

            std::string extra = lambda();
            if (! extra.empty())
                ss << "; " << extra;

            JLOG(debugLog().fatal()) << ss.str();
            std::cerr << ss.str() << std::endl;
            throw;
        };

    try
    {
        // Call passed in member function.
        ((t)->*(threadTop)) ();
    }
    catch (std::exception const& ex)
    {
        logUncaughtException (ex.what());
    }
    catch (boost::coroutines::detail::forced_unwind const&)
    {
        logUncaughtException ("forced_unwind");
    }
    catch (...)
    {
        logUncaughtException ("unknown exception type");
    }
#endif // NO_LOG_UNHANDLED_EXCEPTIONS else
}

// Handle the common case where there is no additional local information.
template <typename T, typename R>
inline void reportUncaughtException (
    T* t, R (T::*threadTop) (), char const* name)
{
    reportUncaughtException (t, threadTop, name, []{ return std::string(); });
}

} // namespace ripple

#endif
