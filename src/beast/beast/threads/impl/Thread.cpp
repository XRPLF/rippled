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

#include <beast/threads/Thread.h>
#include <beast/smart_ptr/SharedObject.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <beast/module/core/time/Time.h>

#include <cassert>

namespace beast {

Thread::Thread (const String& threadName_)
    : threadName (threadName_),
      threadHandle (nullptr),
      threadId (0),
      threadPriority (5),
      affinityMask (0),
      shouldExit (false)
{
}

Thread::~Thread()
{
    /* If your thread class's destructor has been called without first stopping the thread, that
       means that this partially destructed object is still performing some work - and that's
       probably a Bad Thing!

       To avoid this type of nastiness, always make sure you call stopThread() before or during
       your subclass's destructor.
    */
    assert (! isThreadRunning());

    stopThread ();
}

//==============================================================================
// Use a ref-counted object to hold this shared data, so that it can outlive its static
// shared pointer when threads are still running during static shutdown.
struct CurrentThreadHolder : public SharedObject
{
    CurrentThreadHolder() noexcept {}

    typedef SharedPtr <CurrentThreadHolder> Ptr;
    ThreadLocalValue<Thread*> value;
};

static char currentThreadHolderLock [sizeof (SpinLock)]; // (statically initialised to zeros).

static SpinLock* castToSpinLockWithoutAliasingWarning (void* s)
{
    return static_cast<SpinLock*> (s);
}

static CurrentThreadHolder::Ptr getCurrentThreadHolder()
{
    static CurrentThreadHolder::Ptr currentThreadHolder;
    SpinLock::ScopedLockType lock (*castToSpinLockWithoutAliasingWarning (currentThreadHolderLock));

    if (currentThreadHolder == nullptr)
        currentThreadHolder = new CurrentThreadHolder();

    return currentThreadHolder;
}

void Thread::threadEntryPoint()
{
    const CurrentThreadHolder::Ptr currentThreadHolder (getCurrentThreadHolder());
    currentThreadHolder->value = this;

    if (threadName.isNotEmpty())
        setCurrentThreadName (threadName);

    if (startSuspensionEvent.wait (10000))
    {
        bassert (getCurrentThreadId() == threadId);

        if (affinityMask != 0)
            setCurrentThreadAffinityMask (affinityMask);

        run();
    }

    currentThreadHolder->value.releaseCurrentThreadStorage();
    closeThreadHandle();
}

// used to wrap the incoming call from the platform-specific code
void beast_threadEntryPoint (void* userData)
{
    static_cast <Thread*> (userData)->threadEntryPoint();
}

//==============================================================================
void Thread::startThread()
{
    const RecursiveMutex::ScopedLockType  sl (startStopLock);

    shouldExit = false;

    if (threadHandle == nullptr)
    {
        launchThread();
        setThreadPriority (threadHandle, threadPriority);
        startSuspensionEvent.signal();
    }
}

void Thread::startThread (const int priority)
{
    const RecursiveMutex::ScopedLockType sl (startStopLock);

    if (threadHandle == nullptr)
    {
        threadPriority = priority;
        startThread();
    }
    else
    {
        setPriority (priority);
    }
}

bool Thread::isThreadRunning() const
{
    return threadHandle != nullptr;
}

Thread* Thread::getCurrentThread()
{
    return getCurrentThreadHolder()->value.get();
}

//==============================================================================
void Thread::signalThreadShouldExit()
{
    shouldExit = true;
}

bool Thread::waitForThreadToExit (const int timeOutMilliseconds) const
{
    // Doh! So how exactly do you expect this thread to wait for itself to stop??
    bassert (getThreadId() != getCurrentThreadId() || getCurrentThreadId() == 0);

    const std::uint32_t timeoutEnd = Time::getMillisecondCounter() + (std::uint32_t) timeOutMilliseconds;

    while (isThreadRunning())
    {
        if (timeOutMilliseconds >= 0 && Time::getMillisecondCounter() > timeoutEnd)
            return false;

        sleep (2);
    }

    return true;
}

bool Thread::stopThread (const int timeOutMilliseconds)
{
    bool cleanExit = true;

    // agh! You can't stop the thread that's calling this method! How on earth
    // would that work??
    bassert (getCurrentThreadId() != getThreadId());

    const RecursiveMutex::ScopedLockType sl (startStopLock);

    if (isThreadRunning())
    {
        signalThreadShouldExit();
        notify();

        if (timeOutMilliseconds != 0)
        {
            cleanExit = waitForThreadToExit (timeOutMilliseconds);
        }

        if (isThreadRunning())
        {
            bassert (! cleanExit);

            // very bad karma if this point is reached, as there are bound to be
            // locks and events left in silly states when a thread is killed by force..
            killThread();

            threadHandle = nullptr;
            threadId = 0;

            cleanExit = false;
        }
        else
        {
            cleanExit = true;
        }
    }

    return cleanExit;
}

void Thread::stopThreadAsync ()
{
    const RecursiveMutex::ScopedLockType sl (startStopLock);

    if (isThreadRunning())
    {
        signalThreadShouldExit();
        notify();
    }
}

//==============================================================================
bool Thread::setPriority (const int newPriority)
{
    // NB: deadlock possible if you try to set the thread prio from the thread itself,
    // so using setCurrentThreadPriority instead in that case.
    if (getCurrentThreadId() == getThreadId())
        return setCurrentThreadPriority (newPriority);

    const RecursiveMutex::ScopedLockType sl (startStopLock);

    if (setThreadPriority (threadHandle, newPriority))
    {
        threadPriority = newPriority;
        return true;
    }

    return false;
}

bool Thread::setCurrentThreadPriority (const int newPriority)
{
    return setThreadPriority (0, newPriority);
}

void Thread::setAffinityMask (const std::uint32_t newAffinityMask)
{
    affinityMask = newAffinityMask;
}

//==============================================================================
bool Thread::wait (const int timeOutMilliseconds) const
{
    return defaultEvent.wait (timeOutMilliseconds);
}

void Thread::notify() const
{
    defaultEvent.signal();
}

//==============================================================================

// This is here so we dont have circular includes
//
void SpinLock::enter() const noexcept
{
    if (! tryEnter())
    {
        for (int i = 20; --i >= 0;)
            if (tryEnter())
                return;

        while (! tryEnter())
            Thread::yield();
    }
}

}

//------------------------------------------------------------------------------

#if BEAST_WINDOWS

#include <windows.h>
#include <process.h>
#include <tchar.h>

namespace beast {

HWND beast_messageWindowHandle = 0;  // (this is used by other parts of the codebase)

void beast_threadEntryPoint (void*);

static unsigned int __stdcall threadEntryProc (void* userData)
{
    if (beast_messageWindowHandle != 0)
        AttachThreadInput (GetWindowThreadProcessId (beast_messageWindowHandle, 0),
                           GetCurrentThreadId(), TRUE);

    beast_threadEntryPoint (userData);

    _endthreadex (0);
    return 0;
}

void Thread::launchThread()
{
    unsigned int newThreadId;
    threadHandle = (void*) _beginthreadex (0, 0, &threadEntryProc, this, 0, &newThreadId);
    threadId = (ThreadID) newThreadId;
}

void Thread::closeThreadHandle()
{
    CloseHandle ((HANDLE) threadHandle);
    threadId = 0;
    threadHandle = 0;
}

void Thread::killThread()
{
    if (threadHandle != 0)
    {
       #if BEAST_DEBUG
        OutputDebugStringA ("** Warning - Forced thread termination **\n");
       #endif
        TerminateThread (threadHandle, 0);
    }
}

void Thread::setCurrentThreadName (const String& name)
{
   #if BEAST_DEBUG && BEAST_MSVC
    struct
    {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    } info;

    info.dwType = 0x1000;
    info.szName = name.toUTF8();
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;

    __try
    {
        RaiseException (0x406d1388 /*MS_VC_EXCEPTION*/, 0, sizeof (info) / sizeof (ULONG_PTR), (ULONG_PTR*) &info);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION)
    {}
   #else
    (void) name;
   #endif
}

Thread::ThreadID Thread::getCurrentThreadId()
{
    return (ThreadID) (std::intptr_t) GetCurrentThreadId();
}

bool Thread::setThreadPriority (void* handle, int priority)
{
    int pri = THREAD_PRIORITY_TIME_CRITICAL;

    if (priority < 1)       pri = THREAD_PRIORITY_IDLE;
    else if (priority < 2)  pri = THREAD_PRIORITY_LOWEST;
    else if (priority < 5)  pri = THREAD_PRIORITY_BELOW_NORMAL;
    else if (priority < 7)  pri = THREAD_PRIORITY_NORMAL;
    else if (priority < 9)  pri = THREAD_PRIORITY_ABOVE_NORMAL;
    else if (priority < 10) pri = THREAD_PRIORITY_HIGHEST;

    if (handle == 0)
        handle = GetCurrentThread();

    return SetThreadPriority (handle, pri) != FALSE;
}

void Thread::setCurrentThreadAffinityMask (const std::uint32_t affinityMask)
{
    SetThreadAffinityMask (GetCurrentThread(), affinityMask);
}

struct SleepEvent
{
    SleepEvent() noexcept
        : handle (CreateEvent (nullptr, FALSE, FALSE,
                              #if BEAST_DEBUG
                               _T("BEAST Sleep Event")))
                              #else
                               nullptr))
                              #endif
    {}

    ~SleepEvent() noexcept
    {
        CloseHandle (handle);
        handle = 0;
    }

    HANDLE handle;
};

static SleepEvent sleepEvent;

void Thread::sleep (const int millisecs)
{
    if (millisecs >= 10 || sleepEvent.handle == 0)
    {
        Sleep ((DWORD) millisecs);
    }
    else
    {
        // unlike Sleep() this is guaranteed to return to the current thread after
        // the time expires, so we'll use this for short waits, which are more likely
        // to need to be accurate
        WaitForSingleObject (sleepEvent.handle, (DWORD) millisecs);
    }
}

void Thread::yield()
{
    Sleep (0);
}

}

//------------------------------------------------------------------------------

#else

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <time.h>
#if BEAST_BSD
 // ???
#elif BEAST_MAC || BEAST_IOS
#include <Foundation/NSThread.h>
#include <Foundation/NSString.h>
#import <objc/message.h>
namespace beast{
#include <beast/module/core/native/osx_ObjCHelpers.h>
}

#else
#include <sys/prctl.h>

#endif

namespace beast {

void Thread::sleep (int millisecs)
{
    struct timespec time;
    time.tv_sec = millisecs / 1000;
    time.tv_nsec = (millisecs % 1000) * 1000000;
    nanosleep (&time, nullptr);
}

void beast_threadEntryPoint (void*);

extern "C" void* threadEntryProcBeast (void*);
extern "C" void* threadEntryProcBeast (void* userData)
{
    BEAST_AUTORELEASEPOOL
    {
       #if BEAST_ANDROID
        struct AndroidThreadScope
        {
            AndroidThreadScope()   { threadLocalJNIEnvHolder.attach(); }
            ~AndroidThreadScope()  { threadLocalJNIEnvHolder.detach(); }
        };

        const AndroidThreadScope androidEnv;
       #endif

        beast_threadEntryPoint (userData);
    }

    return nullptr;
}

void Thread::launchThread()
{
    threadHandle = 0;
    pthread_t handle = 0;

    if (pthread_create (&handle, 0, threadEntryProcBeast, this) == 0)
    {
        pthread_detach (handle);
        threadHandle = (void*) handle;
        threadId = (ThreadID) threadHandle;
    }
}

void Thread::closeThreadHandle()
{
    threadId = 0;
    threadHandle = 0;
}

void Thread::killThread()
{
    if (threadHandle != 0)
    {
       #if BEAST_ANDROID
        bassertfalse; // pthread_cancel not available!
       #else
        pthread_cancel ((pthread_t) threadHandle);
       #endif
    }
}

void Thread::setCurrentThreadName (const String& name)
{
   #if BEAST_IOS || (BEAST_MAC && defined (MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    BEAST_AUTORELEASEPOOL
    {
        [[NSThread currentThread] setName: beastStringToNS (name)];
    }
   #elif BEAST_LINUX
    #if (__GLIBC__ * 1000 + __GLIBC_MINOR__) >= 2012
     pthread_setname_np (pthread_self(), name.toRawUTF8());
    #else
     prctl (PR_SET_NAME, name.toRawUTF8(), 0, 0, 0);
    #endif
   #endif
}

bool Thread::setThreadPriority (void* handle, int priority)
{
    struct sched_param param;
    int policy;
    priority = blimit (0, 10, priority);

    if (handle == nullptr)
        handle = (void*) pthread_self();

    if (pthread_getschedparam ((pthread_t) handle, &policy, &param) != 0)
        return false;

    policy = priority == 0 ? SCHED_OTHER : SCHED_RR;

    const int minPriority = sched_get_priority_min (policy);
    const int maxPriority = sched_get_priority_max (policy);

    param.sched_priority = ((maxPriority - minPriority) * priority) / 10 + minPriority;
    return pthread_setschedparam ((pthread_t) handle, policy, &param) == 0;
}

Thread::ThreadID Thread::getCurrentThreadId()
{
    return (ThreadID) pthread_self();
}

void Thread::yield()
{
    sched_yield();
}

//==============================================================================
/* Remove this macro if you're having problems compiling the cpu affinity
   calls (the API for these has changed about quite a bit in various Linux
   versions, and a lot of distros seem to ship with obsolete versions)
*/
#if defined (CPU_ISSET) && ! defined (SUPPORT_AFFINITIES)
 #define SUPPORT_AFFINITIES 1
#endif

void Thread::setCurrentThreadAffinityMask (const std::uint32_t affinityMask)
{
   #if SUPPORT_AFFINITIES
    cpu_set_t affinity;
    CPU_ZERO (&affinity);

    for (int i = 0; i < 32; ++i)
        if ((affinityMask & (1 << i)) != 0)
            CPU_SET (i, &affinity);

    /*
       N.B. If this line causes a compile error, then you've probably not got the latest
       version of glibc installed.

       If you don't want to update your copy of glibc and don't care about cpu affinities,
       then you can just disable all this stuff by setting the SUPPORT_AFFINITIES macro to 0.
    */
    sched_setaffinity (getpid(), sizeof (cpu_set_t), &affinity);
    sched_yield();

   #else
    /* affinities aren't supported because either the appropriate header files weren't found,
       or the SUPPORT_AFFINITIES macro was turned off
    */
    bassertfalse;
    (void) affinityMask;
   #endif
}

}

//------------------------------------------------------------------------------

#endif

