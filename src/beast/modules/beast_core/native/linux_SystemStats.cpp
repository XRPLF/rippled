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

void Logger::outputDebugString (const String& text)
{
    std::cerr << text << std::endl;
}

//==============================================================================
SystemStats::OperatingSystemType SystemStats::getOperatingSystemType()
{
    return Linux;
}

String SystemStats::getOperatingSystemName()
{
    return "Linux";
}

bool SystemStats::isOperatingSystem64Bit()
{
   #if BEAST_64BIT
    return true;
   #else
    //xxx not sure how to find this out?..
    return false;
   #endif
}

//==============================================================================
namespace LinuxStatsHelpers
{
    String getCpuInfo (const char* const key)
    {
        StringArray lines;
        File ("/proc/cpuinfo").readLines (lines);

        for (int i = lines.size(); --i >= 0;) // (NB - it's important that this runs in reverse order)
            if (lines[i].startsWithIgnoreCase (key))
                return lines[i].fromFirstOccurrenceOf (":", false, false).trim();

        return String::empty;
    }
}

String SystemStats::getCpuVendor()
{
    return LinuxStatsHelpers::getCpuInfo ("vendor_id");
}

int SystemStats::getCpuSpeedInMegaherz()
{
    return roundToInt (LinuxStatsHelpers::getCpuInfo ("cpu MHz").getFloatValue());
}

int SystemStats::getMemorySizeInMegabytes()
{
    struct sysinfo sysi;

    if (sysinfo (&sysi) == 0)
        return sysi.totalram * sysi.mem_unit / (1024 * 1024);

    return 0;
}

int SystemStats::getPageSize()
{
    return sysconf (_SC_PAGESIZE);
}

//==============================================================================
String SystemStats::getLogonName()
{
    const char* user = getenv ("USER");

    if (user == nullptr)
        if (passwd* const pw = getpwuid (getuid()))
            user = pw->pw_name;

    return CharPointer_UTF8 (user);
}

String SystemStats::getFullUserName()
{
    return getLogonName();
}

String SystemStats::getComputerName()
{
    char name [256] = { 0 };
    if (gethostname (name, sizeof (name) - 1) == 0)
        return name;

    return String::empty;
}

static String getLocaleValue (nl_item key)
{
    const char* oldLocale = ::setlocale (LC_ALL, "");
    String result (String::fromUTF8 (nl_langinfo (key)));
    ::setlocale (LC_ALL, oldLocale);
    return result;
}

String SystemStats::getUserLanguage()    { return getLocaleValue (_NL_IDENTIFICATION_LANGUAGE); }
String SystemStats::getUserRegion()      { return getLocaleValue (_NL_IDENTIFICATION_TERRITORY); }
String SystemStats::getDisplayLanguage() { return getUserLanguage(); }

//==============================================================================
void CPUInformation::initialise() noexcept
{
    const String flags (LinuxStatsHelpers::getCpuInfo ("flags"));
    hasMMX = flags.contains ("mmx");
    hasSSE = flags.contains ("sse");
    hasSSE2 = flags.contains ("sse2");
    hasSSE3 = flags.contains ("sse3");
    has3DNow = flags.contains ("3dnow");

    numCpus = LinuxStatsHelpers::getCpuInfo ("processor").getIntValue() + 1;
}

//==============================================================================
uint32 beast_millisecondsSinceStartup() noexcept
{
    timespec t;
    clock_gettime (CLOCK_MONOTONIC, &t);

    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int64 Time::getHighResolutionTicks() noexcept
{
    timespec t;
    clock_gettime (CLOCK_MONOTONIC, &t);

    return (t.tv_sec * (int64) 1000000) + (t.tv_nsec / 1000);
}

int64 Time::getHighResolutionTicksPerSecond() noexcept
{
    return 1000000;  // (microseconds)
}

double Time::getMillisecondCounterHiRes() noexcept
{
    return getHighResolutionTicks() * 0.001;
}

bool Time::setSystemTimeToThisTime() const
{
    timeval t;
    t.tv_sec = millisSinceEpoch / 1000;
    t.tv_usec = (millisSinceEpoch - t.tv_sec * 1000) * 1000;

    return settimeofday (&t, 0) == 0;
}
