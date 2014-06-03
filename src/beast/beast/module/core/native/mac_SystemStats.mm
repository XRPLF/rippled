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
void Logger::outputDebugString (const String& text)
{
    // Would prefer to use std::cerr here, but avoiding it for
    // the moment, due to clang JIT linkage problems.
    fputs (text.toRawUTF8(), stderr);
    fputs ("\n", stderr);
    fflush (stderr);
}

//==============================================================================
namespace SystemStatsHelpers
{
   #if BEAST_INTEL && ! BEAST_NO_INLINE_ASM
    static void doCPUID (std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d, std::uint32_t type)
    {
        std::uint32_t la = a, lb = b, lc = c, ld = d;

        asm ("mov %%ebx, %%esi \n\t"
             "cpuid \n\t"
             "xchg %%esi, %%ebx"
               : "=a" (la), "=S" (lb), "=c" (lc), "=d" (ld) : "a" (type)
           #if BEAST_64BIT
                  , "b" (lb), "c" (lc), "d" (ld)
           #endif
        );

        a = la; b = lb; c = lc; d = ld;
    }
   #endif
}

//==============================================================================
void CPUInformation::initialise() noexcept
{
   #if BEAST_INTEL && ! BEAST_NO_INLINE_ASM
    std::uint32_t a = 0, b = 0, d = 0, c = 0;
    SystemStatsHelpers::doCPUID (a, b, c, d, 1);

    hasMMX = (d & (1u << 23)) != 0;
    hasSSE = (d & (1u << 25)) != 0;
    hasSSE2 = (d & (1u << 26)) != 0;
    has3DNow = (b & (1u << 31)) != 0;
    hasSSE3 = (c & (1u << 0)) != 0;
   #endif

   #if BEAST_IOS || (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    numCpus = (int) [[NSProcessInfo processInfo] activeProcessorCount];
   #else
    numCpus = (int) MPProcessors();
   #endif
}

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
#if ! BEAST_IOS
static String getOSXVersion()
{
    BEAST_AUTORELEASEPOOL
    {
        NSDictionary* dict = [NSDictionary dictionaryWithContentsOfFile:
                                    nsStringLiteral ("/System/Library/CoreServices/SystemVersion.plist")];

        return nsStringToBeast ([dict objectForKey: nsStringLiteral ("ProductVersion")]);
    }
}
#endif

SystemStats::OperatingSystemType SystemStats::getOperatingSystemType()
{
   #if BEAST_IOS
    return iOS;
   #else
    StringArray parts;
    parts.addTokens (getOSXVersion(), ".", String::empty);

    bassert (parts[0].getIntValue() == 10);
    const int major = parts[1].getIntValue();
    bassert (major > 2);

    return (OperatingSystemType) (major + MacOSX_10_4 - 4);
   #endif
}

String SystemStats::getOperatingSystemName()
{
   #if BEAST_IOS
    return "iOS " + nsStringToBeast ([[UIDevice currentDevice] systemVersion]);
   #else
    return "Mac OSX " + getOSXVersion();
   #endif
}

bool SystemStats::isOperatingSystem64Bit()
{
   #if BEAST_IOS
    return false;
   #elif BEAST_64BIT
    return true;
   #else
    return getOperatingSystemType() >= MacOSX_10_6;
   #endif
}

int SystemStats::getMemorySizeInMegabytes()
{
    std::uint64_t mem = 0;
    size_t memSize = sizeof (mem);
    int mib[] = { CTL_HW, HW_MEMSIZE };
    sysctl (mib, 2, &mem, &memSize, 0, 0);
    return (int) (mem / (1024 * 1024));
}

String SystemStats::getCpuVendor()
{
   #if BEAST_INTEL && ! BEAST_NO_INLINE_ASM
    std::uint32_t dummy = 0;
    std::uint32_t vendor[4] = { 0 };

    SystemStatsHelpers::doCPUID (dummy, vendor[0], vendor[2], vendor[1], 0);

    return String (reinterpret_cast <const char*> (vendor), 12);
   #else
    return String::empty;
   #endif
}

int SystemStats::getCpuSpeedInMegaherz()
{
    std::uint64_t speedHz = 0;
    size_t speedSize = sizeof (speedHz);
    int mib[] = { CTL_HW, HW_CPU_FREQ };
    sysctl (mib, 2, &speedHz, &speedSize, 0, 0);

   #if BEAST_BIG_ENDIAN
    if (speedSize == 4)
        speedHz >>= 32;
   #endif

    return (int) (speedHz / 1000000);
}

//==============================================================================
String SystemStats::getLogonName()
{
    return nsStringToBeast (NSUserName());
}

String SystemStats::getFullUserName()
{
    return nsStringToBeast (NSFullUserName());
}

String SystemStats::getComputerName()
{
    char name [256] = { 0 };
    if (gethostname (name, sizeof (name) - 1) == 0)
        return String (name).upToLastOccurrenceOf (".local", false, true);

    return String::empty;
}

static String getLocaleValue (CFStringRef key)
{
    CFLocaleRef cfLocale = CFLocaleCopyCurrent();
    const String result (String::fromCFString ((CFStringRef) CFLocaleGetValue (cfLocale, key)));
    CFRelease (cfLocale);
    return result;
}

String SystemStats::getUserLanguage()   { return getLocaleValue (kCFLocaleLanguageCode); }
String SystemStats::getUserRegion()     { return getLocaleValue (kCFLocaleCountryCode); }

String SystemStats::getDisplayLanguage()
{
    CFArrayRef cfPrefLangs = CFLocaleCopyPreferredLanguages();
    const String result (String::fromCFString ((CFStringRef) CFArrayGetValueAtIndex (cfPrefLangs, 0)));
    CFRelease (cfPrefLangs);
    return result;
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

bool Time::setSystemTimeToThisTime() const
{
    bassertfalse;
    return false;
}

//==============================================================================
int SystemStats::getPageSize()
{
    return (int) NSPageSize();
}

} // beast
