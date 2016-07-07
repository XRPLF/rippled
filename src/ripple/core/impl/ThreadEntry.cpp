//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/core/ThreadEntry.h>
#include <ripple/basics/Log.h>

#include <boost/coroutine/exceptions.hpp>
#include <boost/thread/tss.hpp>
#include <exception>
#include <iostream>

namespace ripple {

#ifndef NO_LOG_UNHANDLED_EXCEPTIONS
static boost::thread_specific_ptr<std::string> threadName;

namespace detail {
void setThreadName(std::string name)
{
    threadName.reset(new std::string(std::move(name)));
}
}

void terminateHandler()
{
    if (std::current_exception())
    {
        if (!threadName.get())
            detail::setThreadName("Unknown");
        try
        {
            throw;
        }
        catch (const std::exception& e)
        {
            std::cerr << *threadName.get() << ": " << e.what () << '\n';
            JLOG(debugLog().fatal())
                << *threadName.get() << ": " << e.what () << '\n';
        }
        catch (boost::coroutines::detail::forced_unwind const&)
        {
            std::cerr << *threadName.get() << ": forced_unwind\n";
            JLOG(debugLog().fatal())
                << *threadName.get() << ": forced_unwind\n";
        }
        catch (...)
        {
            std::cerr << *threadName.get() << ": unknown exception\n";
            JLOG (debugLog ().fatal ())
                << *threadName.get() << ": unknown exception\n";
        }
    }
}
#endif

}
