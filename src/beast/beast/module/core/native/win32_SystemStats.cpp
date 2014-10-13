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

void outputDebugString (std::string const& text)
{
    OutputDebugStringA ((text + "\n").c_str ());
}

//==============================================================================
std::uint32_t beast_millisecondsSinceStartup() noexcept
{
    return (std::uint32_t) timeGetTime();
}

//==============================================================================
class HiResCounterHandler
{
public:
    HiResCounterHandler()
        : hiResTicksOffset (0)
    {
        const MMRESULT res = timeBeginPeriod (1);
        (void) res;
        bassert (res == TIMERR_NOERROR);

        LARGE_INTEGER f;
        QueryPerformanceFrequency (&f);
        hiResTicksPerSecond = f.QuadPart;
        hiResTicksScaleFactor = 1000.0 / hiResTicksPerSecond;
    }

    inline std::int64_t getHighResolutionTicks() noexcept
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter (&ticks);

        const std::int64_t mainCounterAsHiResTicks = (beast_millisecondsSinceStartup() * hiResTicksPerSecond) / 1000;
        const std::int64_t newOffset = mainCounterAsHiResTicks - ticks.QuadPart;

        std::int64_t offsetDrift = newOffset - hiResTicksOffset;

        // fix for a very obscure PCI hardware bug that can make the counter
        // sometimes jump forwards by a few seconds..
        if (offsetDrift < 0)
            offsetDrift = -offsetDrift;

        if (offsetDrift > (hiResTicksPerSecond >> 1))
            hiResTicksOffset = newOffset;

        return ticks.QuadPart + hiResTicksOffset;
    }

    inline double getMillisecondCounterHiRes() noexcept
    {
        return getHighResolutionTicks() * hiResTicksScaleFactor;
    }

    std::int64_t hiResTicksPerSecond, hiResTicksOffset;
    double hiResTicksScaleFactor;
};

static HiResCounterHandler hiResCounterHandler;

std::int64_t  Time::getHighResolutionTicksPerSecond() noexcept  { return hiResCounterHandler.hiResTicksPerSecond; }
std::int64_t  Time::getHighResolutionTicks() noexcept           { return hiResCounterHandler.getHighResolutionTicks(); }
double Time::getMillisecondCounterHiRes() noexcept       { return hiResCounterHandler.getMillisecondCounterHiRes(); }

//==============================================================================
std::string SystemStats::getComputerName()
{
    char text [MAX_COMPUTERNAME_LENGTH + 2] = { 0 };
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameA (text, &len))
        text[0] = 0;
    return text;
}

} // beast
