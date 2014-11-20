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

#include <beast/module/core/diagnostic/FatalError.h>

#include <atomic>
#include <exception>
#include <iostream>
#include <mutex>

namespace beast {

//------------------------------------------------------------------------------
void
FatalError (char const* message, char const* file, int line)
{
    static std::atomic <int> error_count (0);
    static std::recursive_mutex gate;

    // We only allow one thread to report a fatal error. Other threads that
    // encounter fatal errors while we are reporting get blocked here.
    std::lock_guard<std::recursive_mutex> lock(gate);

    // If we encounter a recursive fatal error, then we want to terminate
    // unconditionally.
    if (error_count++ != 0)
        return std::terminate ();

    // We protect this entire block of code since writing to cerr might trigger
    // exceptions.
    try
    {
        std::cerr << "An error has occurred. The application will terminate.\n";

        if (message != nullptr && message [0] != 0)
            std::cerr << "Message: " << message << '\n';

        if (file != nullptr && file [0] != 0)
            std::cerr << "   File: " << file << ":" << line << '\n';

        auto const backtrace = SystemStats::getStackBacktrace ();

        if (!backtrace.empty ())
        {
            std::cerr << "  Stack:" << std::endl;

            for (auto const& frame : backtrace)
                std::cerr << "    " << frame << '\n';
        }
    }
    catch (...)
    {
        // nothing we can do - just fall through and terminate
    }

    return std::terminate ();
}

} // beast
