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

// Some basic tests, to keep an eye on things and make sure these types work ok
// on all platforms.

static_assert (sizeof (std::intptr_t) == sizeof (void*), "std::intptr_t must be the same size as void*");

static_assert (sizeof (std::int8_t) == 1,   "std::int8_t must be exactly 1 byte!");
static_assert (sizeof (std::int16_t) == 2,  "std::int16_t must be exactly 2 bytes!");
static_assert (sizeof (std::int32_t) == 4,  "std::int32_t must be exactly 4 bytes!");
static_assert (sizeof (std::int64_t) == 8,  "std::int64_t must be exactly 8 bytes!");

static_assert (sizeof (std::uint8_t) == 1,  "std::uint8_t must be exactly 1 byte!");
static_assert (sizeof (std::uint16_t) == 2, "std::uint16_t must be exactly 2 bytes!");
static_assert (sizeof (std::uint32_t) == 4, "std::uint32_t must be exactly 4 bytes!");
static_assert (sizeof (std::uint64_t) == 8, "std::uint64_t must be exactly 8 bytes!");

namespace beast
{

std::string
SystemStats::getBeastVersion()
{
    return "Beast v" + std::to_string (BEAST_MAJOR_VERSION) +
                 "." + std::to_string (BEAST_MINOR_VERSION) +
                 "." + std::to_string (BEAST_BUILDNUMBER);
}

//==============================================================================
std::vector <std::string>
SystemStats::getStackBacktrace()
{
    std::vector <std::string> result;

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
            std::string frame;

            frame.append (std::to_string (i) + ": ");

            IMAGEHLP_MODULE64 moduleInfo;
            zerostruct (moduleInfo);
            moduleInfo.SizeOfStruct = sizeof (moduleInfo);

            if (::SymGetModuleInfo64 (process, symbol->ModBase, &moduleInfo))
            {
                frame.append (moduleInfo.ModuleName);
                frame.append (": ");
            }

            frame.append (symbol->Name);

            if (displacement)
            {
                frame.append ("+");
                frame.append (std::to_string (displacement));
            }

            result.push_back (frame);
        }
    }

#else
    void* stack[128];
    int frames = backtrace (stack, numElementsInArray (stack));
    char** frameStrings = backtrace_symbols (stack, frames);

    for (int i = 0; i < frames; ++i)
        result.push_back (frameStrings[i]);

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
