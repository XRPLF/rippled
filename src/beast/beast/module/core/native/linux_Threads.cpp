//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

namespace beast
{

/*
    Note that a lot of methods that you'd expect to find in this file actually
    live in beast_posix_SharedCode.h!
*/

//==============================================================================
void Process::setPriority (const ProcessPriority prior)
{
    const int policy = (prior <= NormalPriority) ? SCHED_OTHER : SCHED_RR;
    const int minp = sched_get_priority_min (policy);
    const int maxp = sched_get_priority_max (policy);

    struct sched_param param;

    switch (prior)
    {
        case LowPriority:
        case NormalPriority:    param.sched_priority = 0; break;
        case HighPriority:      param.sched_priority = minp + (maxp - minp) / 4; break;
        case RealtimePriority:  param.sched_priority = minp + (3 * (maxp - minp) / 4); break;
        default:                bassertfalse; break;
    }

    pthread_setschedparam (pthread_self(), policy, &param);
}

bool beast_isRunningUnderDebugger()
{
    static char testResult = 0;

    if (testResult == 0)
    {
        testResult = (char) ptrace (PT_TRACE_ME, 0, 0, 0);

        if (testResult >= 0)
        {
            ptrace (PT_DETACH, 0, (caddr_t) 1, 0);
            testResult = 1;
        }
    }

    return testResult < 0;
}

bool Process::isRunningUnderDebugger()
{
    return beast_isRunningUnderDebugger();
}

// TODO(tom): raisePrivilege and lowerPrivilege don't seem to be called.  If we
// start using them, we should deal with the return codes of setreuid() and
// setregid().
static bool swapUserAndEffectiveUser()
{
    auto r1 = setreuid (geteuid(), getuid());
    auto r2 = setregid (getegid(), getgid());
    return !(r1 || r2);
}

void Process::raisePrivilege()  { if (geteuid() != 0 && getuid() == 0) swapUserAndEffectiveUser(); }
void Process::lowerPrivilege()  { if (geteuid() == 0 && getuid() != 0) swapUserAndEffectiveUser(); }

} // beast
