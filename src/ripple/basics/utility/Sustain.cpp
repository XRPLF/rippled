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

#include <ripple/basics/utility/ThreadName.h>
    
// For Sustain Linux variants
// VFALCO TODO Rewrite Sustain to use beast::Process
#ifdef __linux__
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif
#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace ripple {

#ifdef __unix__

static pid_t pManager = static_cast<pid_t> (0);
static pid_t pChild = static_cast<pid_t> (0);

static void pass_signal (int a)
{
    kill (pChild, a);
}

static void stop_manager (int)
{
    kill (pChild, SIGINT);
    _exit (0);
}

bool HaveSustain ()
{
    return true;
}

std::string StopSustain ()
{
    if (getppid () != pManager)
        return std::string ();

    kill (pManager, SIGHUP);
    return "Terminating monitor";
}

std::string DoSustain (std::string logFile)
{
    int childCount = 0;
    pManager = getpid ();
    signal (SIGINT, stop_manager);
    signal (SIGHUP, stop_manager);
    signal (SIGUSR1, pass_signal);
    signal (SIGUSR2, pass_signal);

    while (1)
    {
        ++childCount;
        pChild = fork ();

        if (pChild == -1)
            _exit (0);

        if (pChild == 0)
        {
            setCallingThreadName ("main");
            signal (SIGINT, SIG_DFL);
            signal (SIGHUP, SIG_DFL);
            signal (SIGUSR1, SIG_DFL);
            signal (SIGUSR2, SIG_DFL);
            return str (boost::format ("Launching child %d") % childCount);;
        }

        setCallingThreadName (boost::str (boost::format ("#%d") % childCount).c_str ());

        sleep (9);
        do
        {
            int i;
            sleep (1);
            waitpid (pChild, &i, 0);
        }
        while (kill (pChild, 0) == 0);

        rename ("core", boost::str (boost::format ("core.%d") % static_cast<int> (pChild)).c_str ());
        if (!logFile.empty()) // FIXME: logFile hasn't been set yet
            rename (logFile.c_str(),
	        boost::str (boost::format ("%s.%d") % logFile % static_cast<int> (pChild)).c_str ());
    }
}

#else

bool HaveSustain ()
{
    return false;
}
std::string DoSustain (std::string)
{
    return std::string ();
}
std::string StopSustain ()
{
    return std::string ();
}

#endif

} // ripple
