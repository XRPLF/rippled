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

bool BEAST_CALLTYPE Process::isRunningUnderDebugger()
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
class InterProcessLock::Pimpl
{
public:
    Pimpl (String name, const int timeOutMillisecs)
        : handle (0), refCount (1)
    {
        name = name.replaceCharacter ('\\', '/');
        handle = CreateMutexW (0, TRUE, ("Global\\" + name).toWideCharPointer());

        // Not 100% sure why a global mutex sometimes can't be allocated, but if it fails, fall back to
        // a local one. (A local one also sometimes fails on other machines so neither type appears to be
        // universally reliable)
        if (handle == 0)
            handle = CreateMutexW (0, TRUE, ("Local\\" + name).toWideCharPointer());

        if (handle != 0 && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (timeOutMillisecs == 0)
            {
                close();
                return;
            }

            switch (WaitForSingleObject (handle, timeOutMillisecs < 0 ? INFINITE : timeOutMillisecs))
            {
                case WAIT_OBJECT_0:
                case WAIT_ABANDONED:
                    break;

                case WAIT_TIMEOUT:
                default:
                    close();
                    break;
            }
        }
    }

    ~Pimpl()
    {
        close();
    }

    void close()
    {
        if (handle != 0)
        {
            ReleaseMutex (handle);
            CloseHandle (handle);
            handle = 0;
        }
    }

    HANDLE handle;
    int refCount;
};

InterProcessLock::InterProcessLock (const String& name_)
    : name (name_)
{
}

InterProcessLock::~InterProcessLock()
{
}

bool InterProcessLock::enter (const int timeOutMillisecs)
{
    const ScopedLock sl (lock);

    if (pimpl == nullptr)
    {
        pimpl = new Pimpl (name, timeOutMillisecs);

        if (pimpl->handle == 0)
            pimpl = nullptr;
    }
    else
    {
        pimpl->refCount++;
    }

    return pimpl != nullptr;
}

void InterProcessLock::exit()
{
    const ScopedLock sl (lock);

    // Trying to release the lock too many times!
    bassert (pimpl != nullptr);

    if (pimpl != nullptr && --(pimpl->refCount) == 0)
        pimpl = nullptr;
}

//==============================================================================
class ChildProcess::ActiveProcess : LeakChecked <ChildProcess::ActiveProcess>, public Uncopyable
{
public:
    ActiveProcess (const String& command)
        : ok (false), readPipe (0), writePipe (0)
    {
        SECURITY_ATTRIBUTES securityAtts = { 0 };
        securityAtts.nLength = sizeof (securityAtts);
        securityAtts.bInheritHandle = TRUE;

        if (CreatePipe (&readPipe, &writePipe, &securityAtts, 0)
             && SetHandleInformation (readPipe, HANDLE_FLAG_INHERIT, 0))
        {
            STARTUPINFOW startupInfo = { 0 };
            startupInfo.cb = sizeof (startupInfo);
            startupInfo.hStdError  = writePipe;
            startupInfo.hStdOutput = writePipe;
            startupInfo.dwFlags = STARTF_USESTDHANDLES;

            ok = CreateProcess (nullptr, const_cast <LPWSTR> (command.toWideCharPointer()),
                                nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                nullptr, nullptr, &startupInfo, &processInfo) != FALSE;
        }
    }

    ~ActiveProcess()
    {
        if (ok)
        {
            CloseHandle (processInfo.hThread);
            CloseHandle (processInfo.hProcess);
        }

        if (readPipe != 0)
            CloseHandle (readPipe);

        if (writePipe != 0)
            CloseHandle (writePipe);
    }

    bool isRunning() const
    {
        return WaitForSingleObject (processInfo.hProcess, 0) != WAIT_OBJECT_0;
    }

    int read (void* dest, int numNeeded) const
    {
        int total = 0;

        while (ok && numNeeded > 0)
        {
            DWORD available = 0;

            if (! PeekNamedPipe ((HANDLE) readPipe, nullptr, 0, nullptr, &available, nullptr))
                break;

            const int numToDo = bmin ((int) available, numNeeded);

            if (available == 0)
            {
                if (! isRunning())
                    break;

                Thread::yield();
            }
            else
            {
                DWORD numRead = 0;
                if (! ReadFile ((HANDLE) readPipe, dest, numToDo, &numRead, nullptr))
                    break;

                total += numRead;
                dest = addBytesToPointer (dest, numRead);
                numNeeded -= numRead;
            }
        }

        return total;
    }

    bool killProcess() const
    {
        return TerminateProcess (processInfo.hProcess, 0) != FALSE;
    }

    bool ok;

private:
    HANDLE readPipe, writePipe;
    PROCESS_INFORMATION processInfo;
};

bool ChildProcess::start (const String& command)
{
    activeProcess = new ActiveProcess (command);

    if (! activeProcess->ok)
        activeProcess = nullptr;

    return activeProcess != nullptr;
}

bool ChildProcess::start (const StringArray& args)
{
    return start (args.joinIntoString (" "));
}

bool ChildProcess::isRunning() const
{
    return activeProcess != nullptr && activeProcess->isRunning();
}

int ChildProcess::readProcessOutput (void* dest, int numBytes)
{
    return activeProcess != nullptr ? activeProcess->read (dest, numBytes) : 0;
}

bool ChildProcess::kill()
{
    return activeProcess == nullptr || activeProcess->killProcess();
}

//==============================================================================
struct HighResolutionTimer::Pimpl : public Uncopyable
{
    Pimpl (HighResolutionTimer& t) noexcept  : owner (t), periodMs (0)
    {
    }

    ~Pimpl()
    {
        bassert (periodMs == 0);
    }

    void start (int newPeriod)
    {
        if (newPeriod != periodMs)
        {
            stop();
            periodMs = newPeriod;

            TIMECAPS tc;
            if (timeGetDevCaps (&tc, sizeof (tc)) == TIMERR_NOERROR)
            {
                const int actualPeriod = blimit ((int) tc.wPeriodMin, (int) tc.wPeriodMax, newPeriod);

                timerID = timeSetEvent (actualPeriod, tc.wPeriodMin, callbackFunction, (DWORD_PTR) this,
                                        TIME_PERIODIC | TIME_CALLBACK_FUNCTION | 0x100 /*TIME_KILL_SYNCHRONOUS*/);
            }
        }
    }

    void stop()
    {
        periodMs = 0;
        timeKillEvent (timerID);
    }

    HighResolutionTimer& owner;
    int periodMs;

private:
    unsigned int timerID;

    static void __stdcall callbackFunction (UINT, UINT, DWORD_PTR userInfo, DWORD_PTR, DWORD_PTR)
    {
        if (Pimpl* const timer = reinterpret_cast<Pimpl*> (userInfo))
            if (timer->periodMs != 0)
                timer->owner.hiResTimerCallback();
    }
};
