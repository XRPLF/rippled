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

void* getUser32Function (const char* functionName)
{
    HMODULE module = GetModuleHandleA ("user32.dll");
    bassert (module != 0);
    return (void*) GetProcAddress (module, functionName);
}

//==============================================================================
#if ! BEAST_USE_INTRINSICS
// In newer compilers, the inline versions of these are used (in beast_Atomic.h), but in
// older ones we have to actually call the ops as win32 functions..
long beast_InterlockedExchange (volatile long* a, long b) noexcept                { return InterlockedExchange (a, b); }
long beast_InterlockedIncrement (volatile long* a) noexcept                       { return InterlockedIncrement (a); }
long beast_InterlockedDecrement (volatile long* a) noexcept                       { return InterlockedDecrement (a); }
long beast_InterlockedExchangeAdd (volatile long* a, long b) noexcept             { return InterlockedExchangeAdd (a, b); }
long beast_InterlockedCompareExchange (volatile long* a, long b, long c) noexcept { return InterlockedCompareExchange (a, b, c); }

__int64 beast_InterlockedCompareExchange64 (volatile __int64* value, __int64 newValue, __int64 valueToCompare) noexcept
{
    bassertfalse; // This operation isn't available in old MS compiler versions!

    __int64 oldValue = *value;
    if (oldValue == valueToCompare)
        *value = newValue;

    return oldValue;
}

#endif

//==============================================================================

CriticalSection::CriticalSection() noexcept
{
    // (just to check the MS haven't changed this structure and broken things...)
   #if BEAST_VC7_OR_EARLIER
    static_bassert (sizeof (CRITICAL_SECTION) <= 24);
   #else
    static_bassert (sizeof (CRITICAL_SECTION) <= sizeof (section));
   #endif

    InitializeCriticalSection ((CRITICAL_SECTION*) section);
}

CriticalSection::~CriticalSection() noexcept { DeleteCriticalSection ((CRITICAL_SECTION*) section); }
void CriticalSection::enter() const noexcept { EnterCriticalSection ((CRITICAL_SECTION*) section); }
bool CriticalSection::tryEnter() const noexcept { return TryEnterCriticalSection ((CRITICAL_SECTION*) section) != FALSE; }
void CriticalSection::exit() const noexcept { LeaveCriticalSection ((CRITICAL_SECTION*) section); }

//==============================================================================
static int lastProcessPriority = -1;

// called by WindowDriver because Windows does weird things to process priority
// when you swap apps, and this forces an update when the app is brought to the front.
void beast_repeatLastProcessPriority()
{
    if (lastProcessPriority >= 0) // (avoid changing this if it's not been explicitly set by the app..)
    {
        DWORD p;

        switch (lastProcessPriority)
        {
            case Process::LowPriority:          p = IDLE_PRIORITY_CLASS; break;
            case Process::NormalPriority:       p = NORMAL_PRIORITY_CLASS; break;
            case Process::HighPriority:         p = HIGH_PRIORITY_CLASS; break;
            case Process::RealtimePriority:     p = REALTIME_PRIORITY_CLASS; break;
            default:                            bassertfalse; return; // bad priority value
        }

        SetPriorityClass (GetCurrentProcess(), p);
    }
}

void Process::setPriority (ProcessPriority prior)
{
    if (lastProcessPriority != (int) prior)
    {
        lastProcessPriority = (int) prior;
        beast_repeatLastProcessPriority();
    }
}

bool beast_isRunningUnderDebugger()
{
    return IsDebuggerPresent() != FALSE;
}

bool Process::isRunningUnderDebugger()
{
    return beast_isRunningUnderDebugger();
}

//------------------------------------------------------------------------------

static void* currentModuleHandle = nullptr;

void* Process::getCurrentModuleInstanceHandle() noexcept
{
    if (currentModuleHandle == nullptr)
        currentModuleHandle = GetModuleHandleA (nullptr);

    return currentModuleHandle;
}

void Process::setCurrentModuleInstanceHandle (void* const newHandle) noexcept
{
    currentModuleHandle = newHandle;
}

void Process::raisePrivilege()
{
    bassertfalse; // xxx not implemented
}

void Process::lowerPrivilege()
{
    bassertfalse; // xxx not implemented
}

void Process::terminate()
{
   #if BEAST_MSVC && BEAST_CHECK_MEMORY_LEAKS
    _CrtDumpMemoryLeaks();
   #endif

    // bullet in the head in case there's a problem shutting down..
    ExitProcess (0);
}

bool beast_isRunningInWine()
{
    HMODULE ntdll = GetModuleHandleA ("ntdll");
    return ntdll != 0 && GetProcAddress (ntdll, "wine_get_version") != nullptr;
}

//==============================================================================
bool DynamicLibrary::open (const String& name)
{
    close();

    handle = LoadLibrary (name.toWideCharPointer());

    return handle != nullptr;
}

void DynamicLibrary::close()
{
    if (handle != nullptr)
    {
        FreeLibrary ((HMODULE) handle);
        handle = nullptr;
    }
}

void* DynamicLibrary::getFunction (const String& functionName) noexcept
{
    return handle != nullptr ? (void*) GetProcAddress ((HMODULE) handle, functionName.toUTF8()) // (void* cast is required for mingw)
                             : nullptr;
}

//==============================================================================

} // beast
