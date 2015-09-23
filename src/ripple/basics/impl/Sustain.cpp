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
#include <ripple/basics/Sustain.h>
#include <ripple/basics/ThreadName.h>
#include <boost/format.hpp>

// For Sustain Linux variants
// VFALCO TODO Rewrite Sustain to use beast::Process
#ifdef __linux__
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace ripple {

#ifdef __unix__

static auto const sleepBeforeWaiting = 10;
static auto const sleepBetweenWaits = 1;

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
        return "";

    kill (pManager, SIGHUP);
    return "Terminating monitor";
}

std::string DoSustain ()
{
    pManager = getpid ();
    signal (SIGINT, stop_manager);
    signal (SIGHUP, stop_manager);
    signal (SIGUSR1, pass_signal);
    signal (SIGUSR2, pass_signal);

    for (auto childCount = 1;; ++childCount)
    {
        pChild = fork ();

        if (pChild == -1)
            _exit (0);

        auto cc = std::to_string (childCount);
        if (pChild == 0)
        {
            setCallingThreadName ("main");
            signal (SIGINT, SIG_DFL);
            signal (SIGHUP, SIG_DFL);
            signal (SIGUSR1, SIG_DFL);
            signal (SIGUSR2, SIG_DFL);
            return "Launching child " + cc;
        }

        setCallingThreadName (("#" + cc).c_str());

        sleep (sleepBeforeWaiting);

        for (;;)
        {
            int i;
            waitpid (pChild, &i, 0);
            if (kill (pChild, 0))
                break;
            sleep (sleepBetweenWaits);
        }

        auto pc = std::to_string (pChild);
        rename ("core", ("core." + pc).c_str ());
    }
}

#else

bool HaveSustain ()
{
    return false;
}

std::string DoSustain ()
{
    return "";
}

std::string StopSustain ()
{
    return "";
}

#endif

} // ripple
