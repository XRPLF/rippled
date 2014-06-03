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

String SystemStats::getBeastVersion()
{
    // Some basic tests, to keep an eye on things and make sure these types work ok
    // on all platforms. Let me know if any of these assertions fail on your system!
    static_bassert (sizeof (std::intptr_t) == sizeof (void*));
    static_bassert (sizeof (std::int8_t) == 1);
    static_bassert (sizeof (std::uint8_t) == 1);
    static_bassert (sizeof (std::int16_t) == 2);
    static_bassert (sizeof (std::uint16_t) == 2);
    static_bassert (sizeof (std::int32_t) == 4);
    static_bassert (sizeof (std::uint32_t) == 4);
    static_bassert (sizeof (std::int64_t) == 8);
    static_bassert (sizeof (std::uint64_t) == 8);

    return "Beast v" BEAST_STRINGIFY(BEAST_MAJOR_VERSION)
                "." BEAST_STRINGIFY(BEAST_MINOR_VERSION)
                "." BEAST_STRINGIFY(BEAST_BUILDNUMBER);
}

//==============================================================================
struct CPUInformation
{
    CPUInformation() noexcept
        : numCpus (0), hasMMX (false), hasSSE (false),
          hasSSE2 (false), hasSSE3 (false), has3DNow (false)
    {
        initialise();
    }

    void initialise() noexcept;

    int numCpus;
    bool hasMMX, hasSSE, hasSSE2, hasSSE3, has3DNow;
};

static const CPUInformation& getCPUInformation() noexcept
{
    static CPUInformation info;
    return info;
}

int SystemStats::getNumCpus() noexcept { return getCPUInformation().numCpus; }
bool SystemStats::hasMMX() noexcept { return getCPUInformation().hasMMX; }
bool SystemStats::hasSSE() noexcept { return getCPUInformation().hasSSE; }
bool SystemStats::hasSSE2() noexcept { return getCPUInformation().hasSSE2; }
bool SystemStats::hasSSE3() noexcept { return getCPUInformation().hasSSE3; }
bool SystemStats::has3DNow() noexcept { return getCPUInformation().has3DNow; }

//==============================================================================
String SystemStats::getStackBacktrace()
{
    String result;

   #if BEAST_ANDROID || BEAST_MINGW || BEAST_BSD
    bassertfalse; // sorry, not implemented yet!

   #elif BEAST_WINDOWS
    HANDLE process = GetCurrentProcess();
    SymInitialize (process, nullptr, TRUE);

    void* stack[128];
    int frames = (int) CaptureStackBackTrace (0, numElementsInArray (stack), stack, nullptr);

    HeapBlock<SYMBOL_INFO> symbol;
    symbol.calloc (sizeof (SYMBOL_INFO) + 256, 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof (SYMBOL_INFO);

    for (int i = 0; i < frames; ++i)
    {
        DWORD64 displacement = 0;

        if (SymFromAddr (process, (DWORD64) stack[i], &displacement, symbol))
        {
            result << i << ": ";

            IMAGEHLP_MODULE64 moduleInfo;
            zerostruct (moduleInfo);
            moduleInfo.SizeOfStruct = sizeof (moduleInfo);

            if (::SymGetModuleInfo64 (process, symbol->ModBase, &moduleInfo))
                result << moduleInfo.ModuleName << ": ";

            result << symbol->Name << " + 0x" << String::toHexString ((std::int64_t) displacement) << newLine;
        }
    }

   #else
    void* stack[128];
    int frames = backtrace (stack, numElementsInArray (stack));
    char** frameStrings = backtrace_symbols (stack, frames);

    for (int i = 0; i < frames; ++i)
        result << frameStrings[i] << newLine;

    ::free (frameStrings);
   #endif

    return result;
}

//==============================================================================
static SystemStats::CrashHandlerFunction globalCrashHandler = nullptr;

#if BEAST_WINDOWS
static LONG WINAPI handleCrash (LPEXCEPTION_POINTERS)
{
    globalCrashHandler();
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
static void handleCrash (int)
{
    globalCrashHandler();
    kill (getpid(), SIGKILL);
}

int beast_siginterrupt (int sig, int flag);
#endif

void SystemStats::setApplicationCrashHandler (CrashHandlerFunction handler)
{
    bassert (handler != nullptr); // This must be a valid function.
    globalCrashHandler = handler;

   #if BEAST_WINDOWS
    SetUnhandledExceptionFilter (handleCrash);
   #else
    const int signals[] = { SIGFPE, SIGILL, SIGSEGV, SIGBUS, SIGABRT, SIGSYS };

    for (int i = 0; i < numElementsInArray (signals); ++i)
    {
        ::signal (signals[i], handleCrash);
        beast_siginterrupt (signals[i], 1);
    }
   #endif
}

} // beast
