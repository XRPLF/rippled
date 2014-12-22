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
#include <thread>

namespace beast {

Thread::Thread (std::string const& threadName_)
    : threadName (threadName_),
      threadHandle (nullptr),
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
void Thread::threadEntryPoint()
{
    if (!threadName.empty ())
        setCurrentThreadName (threadName);

    if (startSuspensionEvent.wait (10000))
        run();

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
        startSuspensionEvent.signal();
    }
}

bool Thread::isThreadRunning() const
{
    return threadHandle != nullptr;
}

//==============================================================================
void Thread::signalThreadShouldExit()
{
    shouldExit = true;
}

void Thread::waitForThreadToExit () const
{
    while (isThreadRunning())
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
}

void Thread::stopThread ()
{
    const RecursiveMutex::ScopedLockType sl (startStopLock);

    if (isThreadRunning())
    {
        signalThreadShouldExit();
        notify();
        waitForThreadToExit ();
    }
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
bool Thread::wait (const int timeOutMilliseconds) const
{
    return defaultEvent.wait (timeOutMilliseconds);
}

void Thread::notify() const
{
    defaultEvent.signal();
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
}

void Thread::closeThreadHandle()
{
    CloseHandle ((HANDLE) threadHandle);
    threadHandle = 0;
}

void Thread::setCurrentThreadName (std::string const& name)
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
    info.szName = name.c_str ();
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
    }
}

void Thread::closeThreadHandle()
{
    threadHandle = 0;
}

void Thread::setCurrentThreadName (std::string const& name)
{
   #if BEAST_IOS || (BEAST_MAC && defined (MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    BEAST_AUTORELEASEPOOL
    {
        [[NSThread currentThread] setName: beastStringToNS (beast::String (name))];
    }
   #elif BEAST_LINUX
    #if (__GLIBC__ * 1000 + __GLIBC_MINOR__) >= 2012
     pthread_setname_np (pthread_self(), name.c_str ());
    #else
     prctl (PR_SET_NAME, name.c_str (), 0, 0, 0);
    #endif
   #endif
}

}

//------------------------------------------------------------------------------

#endif

