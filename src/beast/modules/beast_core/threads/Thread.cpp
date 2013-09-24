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
    check_precondition (! isThreadRunning());

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
void BEAST_API beast_threadEntryPoint (void* userData)
{
    ProtectedCall (&Thread::threadEntryPoint, static_cast <Thread*> (userData));
}

//==============================================================================
void Thread::startThread()
{
    const ScopedLock sl (startStopLock);

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
    const ScopedLock sl (startStopLock);

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

    const uint32 timeoutEnd = Time::getMillisecondCounter() + (uint32) timeOutMilliseconds;

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

    const ScopedLock sl (startStopLock);

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

//==============================================================================
bool Thread::setPriority (const int newPriority)
{
    // NB: deadlock possible if you try to set the thread prio from the thread itself,
    // so using setCurrentThreadPriority instead in that case.
    if (getCurrentThreadId() == getThreadId())
        return setCurrentThreadPriority (newPriority);

    const ScopedLock sl (startStopLock);

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

void Thread::setAffinityMask (const uint32 newAffinityMask)
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

//==============================================================================

class AtomicTests : public UnitTest
{
public:
    AtomicTests() : UnitTest ("Atomic", "beast") {}

    void runTest()
    {
        beginTestCase ("Misc");

        char a1[7];
        expect (numElementsInArray(a1) == 7);
        int a2[3];
        expect (numElementsInArray(a2) == 3);

        expect (ByteOrder::swap ((uint16) 0x1122) == 0x2211);
        expect (ByteOrder::swap ((uint32) 0x11223344) == 0x44332211);
        expect (ByteOrder::swap ((uint64) literal64bit (0x1122334455667788)) == literal64bit (0x8877665544332211));

        beginTestCase ("int");
        AtomicTester <int>::testInteger (*this);
        beginTestCase ("unsigned int");
        AtomicTester <unsigned int>::testInteger (*this);
        beginTestCase ("int32");
        AtomicTester <int32>::testInteger (*this);
        beginTestCase ("uint32");
        AtomicTester <uint32>::testInteger (*this);
        beginTestCase ("long");
        AtomicTester <long>::testInteger (*this);
        beginTestCase ("void*");
        AtomicTester <void*>::testInteger (*this);
        beginTestCase ("int*");
        AtomicTester <int*>::testInteger (*this);
        beginTestCase ("float");
        AtomicTester <float>::testFloat (*this);
      #if ! BEAST_64BIT_ATOMICS_UNAVAILABLE  // 64-bit intrinsics aren't available on some old platforms
        beginTestCase ("int64");
        AtomicTester <int64>::testInteger (*this);
        beginTestCase ("uint64");
        AtomicTester <uint64>::testInteger (*this);
        beginTestCase ("double");
        AtomicTester <double>::testFloat (*this);
      #endif
    }

    template <typename Type>
    class AtomicTester
    {
    public:
        AtomicTester() {}

        static void testInteger (UnitTest& test)
        {
            Atomic<Type> a, b;
            a.set ((Type) 10);
            test.expect (a.value == (Type) 10);
            test.expect (a.get() == (Type) 10);
            a += (Type) 15;
            test.expect (a.get() == (Type) 25);
            memoryBarrier();
            a -= (Type) 5;
            test.expect (a.get() == (Type) 20);
            test.expect (++a == (Type) 21);
            ++a;
            test.expect (--a == (Type) 21);
            test.expect (a.get() == (Type) 21);
            memoryBarrier();

            testFloat (test);
        }

        static void testFloat (UnitTest& test)
        {
            Atomic<Type> a, b;
            a = (Type) 21;
            memoryBarrier();

            /*  These are some simple test cases to check the atomics - let me know
                if any of these assertions fail on your system!
            */
            test.expect (a.get() == (Type) 21);
            test.expect (a.compareAndSetValue ((Type) 100, (Type) 50) == (Type) 21);
            test.expect (a.get() == (Type) 21);
            test.expect (a.compareAndSetValue ((Type) 101, a.get()) == (Type) 21);
            test.expect (a.get() == (Type) 101);
            test.expect (! a.compareAndSetBool ((Type) 300, (Type) 200));
            test.expect (a.get() == (Type) 101);
            test.expect (a.compareAndSetBool ((Type) 200, a.get()));
            test.expect (a.get() == (Type) 200);

            test.expect (a.exchange ((Type) 300) == (Type) 200);
            test.expect (a.get() == (Type) 300);

            b = a;
            test.expect (b.get() == a.get());
        }
    };
};

static AtomicTests atomicTests;
