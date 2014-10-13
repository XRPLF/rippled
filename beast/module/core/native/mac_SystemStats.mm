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

ScopedAutoReleasePool::ScopedAutoReleasePool()
{
    pool = [[NSAutoreleasePool alloc] init];
}

ScopedAutoReleasePool::~ScopedAutoReleasePool()
{
    [((NSAutoreleasePool*) pool) release];
}

//==============================================================================
void outputDebugString (std::string const& text)
{
    // Would prefer to use std::cerr here, but avoiding it for
    // the moment, due to clang JIT linkage problems.
    fputs (text.c_str (), stderr);
    fputs ("\n", stderr);
    fflush (stderr);
}

//==============================================================================

#if BEAST_MAC
struct RLimitInitialiser
{
    RLimitInitialiser()
    {
        rlimit lim;
        getrlimit (RLIMIT_NOFILE, &lim);
        lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
        setrlimit (RLIMIT_NOFILE, &lim);
    }
};

static RLimitInitialiser rLimitInitialiser;
#endif

//==============================================================================
std::string SystemStats::getComputerName()
{
    char name [256] = { 0 };
    if (gethostname (name, sizeof (name) - 1) == 0)
        return String (name).upToLastOccurrenceOf (".local", false, true).toStdString();

    return std::string{};
}

//==============================================================================
class HiResCounterHandler
{
public:
    HiResCounterHandler()
    {
        mach_timebase_info_data_t timebase;
        (void) mach_timebase_info (&timebase);

        if (timebase.numer % 1000000 == 0)
        {
            numerator   = timebase.numer / 1000000;
            denominator = timebase.denom;
        }
        else
        {
            numerator   = timebase.numer;
            denominator = timebase.denom * (std::uint64_t) 1000000;
        }

        highResTimerFrequency = (timebase.denom * (std::uint64_t) 1000000000) / timebase.numer;
        highResTimerToMillisecRatio = numerator / (double) denominator;
    }

    inline std::uint32_t millisecondsSinceStartup() const noexcept
    {
        return (std::uint32_t) ((mach_absolute_time() * numerator) / denominator);
    }

    inline double getMillisecondCounterHiRes() const noexcept
    {
        return mach_absolute_time() * highResTimerToMillisecRatio;
    }

    std::int64_t highResTimerFrequency;

private:
    std::uint64_t numerator, denominator;
    double highResTimerToMillisecRatio;
};

static HiResCounterHandler& hiResCounterHandler()
{
    static HiResCounterHandler hiResCounterHandler;
    return hiResCounterHandler;
}

std::uint32_t beast_millisecondsSinceStartup() noexcept         { return hiResCounterHandler().millisecondsSinceStartup(); }
double Time::getMillisecondCounterHiRes() noexcept      { return hiResCounterHandler().getMillisecondCounterHiRes(); }
std::int64_t  Time::getHighResolutionTicksPerSecond() noexcept { return hiResCounterHandler().highResTimerFrequency; }
std::int64_t  Time::getHighResolutionTicks() noexcept          { return (std::int64_t) mach_absolute_time(); }

} // beast
