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

#ifndef RIPPLE_CORE_THREAD_ENTRY_H_INCLUDED
#define RIPPLE_CORE_THREAD_ENTRY_H_INCLUDED

#include <string>

namespace ripple
{

#ifndef NO_LOG_UNHANDLED_EXCEPTIONS
namespace detail
{
void setThreadName(std::string name);
}

void terminateHandler();
#endif

/**
Report uncaught exceptions to DebugLog and cerr

The actual reporting occurs in a terminate handler.  This function
stores information about which thread is running in thread local
storage.  That way the terminate handler can report not just the
exception, but also the thread the exception was thrown in.

The idea is to use this routine at the top of a thread, since on
many platforms the stack trace for an uncaught exception on a thread
is almost useless.

For those platforms where the stack trace from an uncaught exception is
useful (e.g., OS X) this routine is turned into a no-op (because the
preprocessor symbol NO_LOG_UNHANDLED_EXCEPTIONS is defined).

Usage example

#include <ripple/core/ThreadEntry.h>
#include <chrono>
#include <exception>
#include <thread>

class ThreadedHandler
{
public:
    void operator() ()
    {
        threadEntry (
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
*/
template <typename T, typename R>
void threadEntry (
    T* t, R (T::*threadTop) (), std::string name)
{
#ifndef NO_LOG_UNHANDLED_EXCEPTIONS
    detail::setThreadName (std::move(name));
#endif
    ((t)->*(threadTop)) ();
}

} // namespace ripple

#endif
