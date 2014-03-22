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

void Logger::outputDebugString (const String& text)
{
    OutputDebugString ((text + "\n").toWideCharPointer());
}

//==============================================================================
#ifdef BEAST_DLL_BUILD
 void* beastDLL_malloc (size_t sz)    { return std::malloc (sz); }
 void  beastDLL_free (void* block)    { std::free (block); }
#endif

//==============================================================================
#if BEAST_USE_INTRINSICS

// CPU info functions using intrinsics...

#pragma intrinsic (__cpuid)
#pragma intrinsic (__rdtsc)

String SystemStats::getCpuVendor()
{
    int info [4];
    __cpuid (info, 0);

    char v [12];
    memcpy (v, info + 1, 4);
    memcpy (v + 4, info + 3, 4);
    memcpy (v + 8, info + 2, 4);

    return String (v, 12);
}

#else

//==============================================================================
// CPU info functions using old fashioned inline asm...

static void beast_getCpuVendor (char* const v)
{
    int vendor[4] = { 0 };

   #if ! BEAST_MINGW
    __try
   #endif
    {
       #if BEAST_GCC
        unsigned int dummy = 0;
        __asm__ ("cpuid" : "=a" (dummy), "=b" (vendor[0]), "=c" (vendor[2]),"=d" (vendor[1]) : "a" (0));
       #else
        __asm
        {
            mov eax, 0
            cpuid
            mov [vendor], ebx
            mov [vendor + 4], edx
            mov [vendor + 8], ecx
        }
       #endif
    }
   #if ! BEAST_MINGW
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
   #endif

    memcpy (v, vendor, 16);
}

String SystemStats::getCpuVendor()
{
    char v [16];
    beast_getCpuVendor (v);
    return String (v, 16);
}
#endif


//==============================================================================
void CPUInformation::initialise() noexcept
{
    hasMMX = IsProcessorFeaturePresent (PF_MMX_INSTRUCTIONS_AVAILABLE) != 0;
    hasSSE = IsProcessorFeaturePresent (PF_XMMI_INSTRUCTIONS_AVAILABLE) != 0;
    hasSSE2 = IsProcessorFeaturePresent (PF_XMMI64_INSTRUCTIONS_AVAILABLE) != 0;
    hasSSE3 = IsProcessorFeaturePresent (13 /*PF_SSE3_INSTRUCTIONS_AVAILABLE*/) != 0;
    has3DNow = IsProcessorFeaturePresent (7 /*PF_AMD3D_INSTRUCTIONS_AVAILABLE*/) != 0;

    SYSTEM_INFO systemInfo;
    GetNativeSystemInfo (&systemInfo);
    numCpus = (int) systemInfo.dwNumberOfProcessors;
}

//==============================================================================
static bool isWindowsVersionOrLater (SystemStats::OperatingSystemType target)
{
    OSVERSIONINFOEX info;
    zerostruct (info);
    info.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEX);

    if (target >= SystemStats::WinVista)
    {
        info.dwMajorVersion = 6;

        switch (target)
        {
            case SystemStats::WinVista:  info.dwMinorVersion = 0; break;
            case SystemStats::Windows7:  info.dwMinorVersion = 1; break;
            case SystemStats::Windows8:  info.dwMinorVersion = 2; break;
            default:                     bassertfalse; break;
        }
    }
    else
    {
        info.dwMajorVersion = 5;
        info.dwMinorVersion = target >= SystemStats::WinXP ? 1 : 0;
    }

    DWORDLONG mask = 0;

    VER_SET_CONDITION (mask, VER_MAJORVERSION,     VER_GREATER_EQUAL);
    VER_SET_CONDITION (mask, VER_MINORVERSION,     VER_GREATER_EQUAL);
    VER_SET_CONDITION (mask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
    VER_SET_CONDITION (mask, VER_SERVICEPACKMINOR, VER_GREATER_EQUAL);

    return VerifyVersionInfo (&info,
                              VER_MAJORVERSION | VER_MINORVERSION
                               | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR,
                              mask) != FALSE;
}

SystemStats::OperatingSystemType SystemStats::getOperatingSystemType()
{
    const SystemStats::OperatingSystemType types[]
            = { Windows8, Windows7, WinVista, WinXP, Win2000 };

    for (int i = 0; i < numElementsInArray (types); ++i)
        if (isWindowsVersionOrLater (types[i]))
            return types[i];

    bassertfalse;  // need to support whatever new version is running!
    return UnknownOS;
}

String SystemStats::getOperatingSystemName()
{
    const char* name = "Unknown OS";

    switch (getOperatingSystemType())
    {
        case Windows7:          name = "Windows 7";         break;
        case Windows8:          name = "Windows 8";         break;
        case WinVista:          name = "Windows Vista";     break;
        case WinXP:             name = "Windows XP";        break;
        case Win2000:           name = "Windows 2000";      break;
        default:                bassertfalse; break; // !! new type of OS?
    }

    return name;
}

bool SystemStats::isOperatingSystem64Bit()
{
   #if BEAST_64BIT
    return true;
   #else
    typedef BOOL (WINAPI* LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

    LPFN_ISWOW64PROCESS fnIsWow64Process
        = (LPFN_ISWOW64PROCESS) GetProcAddress (GetModuleHandleA ("kernel32"), "IsWow64Process");

    BOOL isWow64 = FALSE;

    return fnIsWow64Process != nullptr
            && fnIsWow64Process (GetCurrentProcess(), &isWow64)
            && isWow64 != FALSE;
   #endif
}

//==============================================================================
int SystemStats::getMemorySizeInMegabytes()
{
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof (mem);
    GlobalMemoryStatusEx (&mem);
    return (int) (mem.ullTotalPhys / (1024 * 1024)) + 1;
}

//==============================================================================
String SystemStats::getEnvironmentVariable (const String& name, const String& defaultValue)
{
    DWORD len = GetEnvironmentVariableW (name.toWideCharPointer(), nullptr, 0);
    if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        return String (defaultValue);

    HeapBlock<WCHAR> buffer (len);
    len = GetEnvironmentVariableW (name.toWideCharPointer(), buffer, len);

    return String (CharPointer_wchar_t (buffer),
                   CharPointer_wchar_t (buffer + len));
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

        // fix for a very obscure PCI hardware bug that can make the counter
        // sometimes jump forwards by a few seconds..
        const std::int64_t offsetDrift = abs64 (newOffset - hiResTicksOffset);

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
static std::int64_t beast_getClockCycleCounter() noexcept
{
   #if BEAST_USE_INTRINSICS
    // MS intrinsics version...
    return (std::int64_t) __rdtsc();

   #elif BEAST_GCC
    // GNU inline asm version...
    unsigned int hi = 0, lo = 0;

    __asm__ __volatile__ (
        "xor %%eax, %%eax               \n\
         xor %%edx, %%edx               \n\
         rdtsc                          \n\
         movl %%eax, %[lo]              \n\
         movl %%edx, %[hi]"
         :
         : [hi] "m" (hi),
           [lo] "m" (lo)
         : "cc", "eax", "ebx", "ecx", "edx", "memory");

    return (std::int64_t) ((((std::uint64_t) hi) << 32) | lo);
   #else
    // MSVC inline asm version...
    unsigned int hi = 0, lo = 0;

    __asm
    {
        xor eax, eax
        xor edx, edx
        rdtsc
        mov lo, eax
        mov hi, edx
    }

    return (std::int64_t) ((((std::uint64_t) hi) << 32) | lo);
   #endif
}

int SystemStats::getCpuSpeedInMegaherz()
{
    const std::int64_t cycles = beast_getClockCycleCounter();
    const std::uint32_t millis = Time::getMillisecondCounter();
    int lastResult = 0;

    for (;;)
    {
        int n = 1000000;
        while (--n > 0) {}

        const std::uint32_t millisElapsed = Time::getMillisecondCounter() - millis;
        const std::int64_t cyclesNow = beast_getClockCycleCounter();

        if (millisElapsed > 80)
        {
            const int newResult = (int) (((cyclesNow - cycles) / millisElapsed) / 1000);

            if (millisElapsed > 500 || (lastResult == newResult && newResult > 100))
                return newResult;

            lastResult = newResult;
        }
    }
}


//==============================================================================
bool Time::setSystemTimeToThisTime() const
{
    SYSTEMTIME st;

    st.wDayOfWeek = 0;
    st.wYear           = (WORD) getYear();
    st.wMonth          = (WORD) (getMonth() + 1);
    st.wDay            = (WORD) getDayOfMonth();
    st.wHour           = (WORD) getHours();
    st.wMinute         = (WORD) getMinutes();
    st.wSecond         = (WORD) getSeconds();
    st.wMilliseconds   = (WORD) (millisSinceEpoch % 1000);

    // do this twice because of daylight saving conversion problems - the
    // first one sets it up, the second one kicks it in.
    return SetLocalTime (&st) != 0
            && SetLocalTime (&st) != 0;
}

int SystemStats::getPageSize()
{
    SYSTEM_INFO systemInfo;
    GetNativeSystemInfo (&systemInfo);

    return (int) systemInfo.dwPageSize;
}

//==============================================================================
String SystemStats::getLogonName()
{
    TCHAR text [256] = { 0 };
    DWORD len = (DWORD) numElementsInArray (text) - 1;
    GetUserName (text, &len);
    return String (text, len);
}

String SystemStats::getFullUserName()
{
    return getLogonName();
}

String SystemStats::getComputerName()
{
    TCHAR text [MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
    DWORD len = (DWORD) numElementsInArray (text) - 1;
    GetComputerName (text, &len);
    return String (text, len);
}

static String getLocaleValue (LCID locale, LCTYPE key, const char* defaultValue)
{
    TCHAR buffer [256] = { 0 };
    if (GetLocaleInfo (locale, key, buffer, 255) > 0)
        return buffer;

    return defaultValue;
}

String SystemStats::getUserLanguage()     { return getLocaleValue (LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME,  "en"); }
String SystemStats::getUserRegion()       { return getLocaleValue (LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, "US"); }

String SystemStats::getDisplayLanguage()
{
    DynamicLibrary dll ("kernel32.dll");
    BEAST_LOAD_WINAPI_FUNCTION (dll, GetUserDefaultUILanguage, getUserDefaultUILanguage, LANGID, (void))

    if (getUserDefaultUILanguage != nullptr)
        return getLocaleValue (MAKELCID (getUserDefaultUILanguage(), SORT_DEFAULT), LOCALE_SISO639LANGNAME, "en");

    return "en";
}

} // beast
