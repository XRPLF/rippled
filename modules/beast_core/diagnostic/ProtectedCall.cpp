//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#if 0
#include <iostream>

//------------------------------------------------------------------------------
//
// Windows structured exception handling
//
#if BEAST_MSVC

#include <windows.h>

namespace vf
{

namespace
{

//
// While this object is in scope, any Windows SEH
// exceptions will be caught and re-thrown as an Error object.
//
class ScopedPlatformExceptionCatcher : public Uncopyable
{
public:
    ScopedPlatformExceptionCatcher ()
    {
        //s_mutex.enter ();

        if (++s_count == 1)
            s_sehPrev = ::SetUnhandledExceptionFilter (sehFilter);

        //s_mutex.exit ();
    }

    ~ScopedPlatformExceptionCatcher ()
    {
        //s_mutex.enter ();

        if (--s_count == 0)
            SetUnhandledExceptionFilter (s_sehPrev);

        //s_mutex.exit ();
    }

    static LONG WINAPI sehFilter (_EXCEPTION_POINTERS* ei)
    {
        EXCEPTION_RECORD* er = ei->ExceptionRecord;

        if (er->ExceptionCode == EXCEPTION_BREAKPOINT ||
                er->ExceptionCode == EXCEPTION_SINGLE_STEP)
        {
            // pass through
        }
        else
        {
            String s;

            switch (er->ExceptionCode)
            {
            case EXCEPTION_ACCESS_VIOLATION:
                s = TRANS ("an access violation occurred");
                break;

            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                s = TRANS ("array bounds were exceeded");
                break;

            case EXCEPTION_DATATYPE_MISALIGNMENT:
                s = TRANS ("memory access was unaligned");
                break;

            case EXCEPTION_FLT_DENORMAL_OPERAND:
                s = TRANS ("a floating point operation produced a denormal");
                break;

            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                s = TRANS ("a floating point divide by zero was attempted");
                break;

            case EXCEPTION_FLT_INEXACT_RESULT:
                s = TRANS ("the floating point operation was unrepresentable");
                break;

            case EXCEPTION_FLT_INVALID_OPERATION:
                s = TRANS ("the floating point operation was invalid");
                break;

            case EXCEPTION_FLT_OVERFLOW:
                s = TRANS ("the floating point operation overflowed");
                break;

            case EXCEPTION_FLT_STACK_CHECK:
                s = TRANS ("a stack check resulted from a floating point operation");
                break;

            case EXCEPTION_FLT_UNDERFLOW:
                s = TRANS ("the floating point operation underflowed");
                break;

            case EXCEPTION_ILLEGAL_INSTRUCTION:
                s = TRANS ("an invalid instruction was received");
                break;

            case EXCEPTION_IN_PAGE_ERROR:
                s = TRANS ("a virtual paging error occurred");
                break;

            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                s = TRANS ("an integer divide by zero was attempted");
                break;

            case EXCEPTION_INT_OVERFLOW:
                s = TRANS ("an integer operation overflowed");
                break;

            case EXCEPTION_INVALID_DISPOSITION:
                s = TRANS ("the exception handler returned an invalid disposition");
                break;

            case EXCEPTION_NONCONTINUABLE_EXCEPTION:
                s = TRANS ("a non-continuable exception occurred");
                break;

            case EXCEPTION_PRIV_INSTRUCTION:
                s = TRANS ("a privileged instruction was attempted");
                break;

            case EXCEPTION_STACK_OVERFLOW:
                s = TRANS ("the stack overflowed");
                break;

            default:
                s = TRANS ("an unknown system exception of code ");
                s << String ((unsigned int)er->ExceptionCode);
                s << " " << TRANS ("occurred");
                break;
            }

            Throw (Error ().fail (__FILE__, __LINE__, s, Error::platform));
        }

        return s_sehPrev (ei);
    }

private:
    static int s_count;
    static CriticalSection s_mutex;
    static LPTOP_LEVEL_EXCEPTION_FILTER s_sehPrev;
};

CriticalSection ScopedPlatformExceptionCatcher::s_mutex;
int ScopedPlatformExceptionCatcher::s_count = 0;
LPTOP_LEVEL_EXCEPTION_FILTER ScopedPlatformExceptionCatcher::s_sehPrev = 0;

}

}

//------------------------------------------------------------------------------

#else

// TODO: POSIX SIGNAL HANDLER

#pragma message(BEAST_FILEANDLINE_ "Missing class ScopedPlatformExceptionCatcher")

namespace vf
{

namespace
{

class ScopedPlatformExceptionCatcher
{
public:
    // Missing
};

}

END_BEAST_NAMESPACE

#endif

#endif

//------------------------------------------------------------------------------

#if 0
bool CatchAny (Function <void (void)> f, bool returnFromException)
{
    bool caughtException = true; // assume the worst

    try
    {
        //ScopedPlatformExceptionCatcher platformExceptionCatcher;

        f ();

        caughtException = false;
    }
    catch (Error& e)
    {
        if (!returnFromException)
        {
            JUCEApplication* app = JUCEApplication::getInstance ();

            if (app)
            {
                app->unhandledException (
                    &e,
                    e.getSourceFilename (),
                    e.getLineNumber ());
            }
            else
            {
                std::cout << e.what ();
                std::unexpected ();
            }
        }
    }
    catch (std::exception& e)
    {
        if (!returnFromException)
        {
            JUCEApplication* app = JUCEApplication::getInstance ();

            if (app)
            {
                app->unhandledException (&e, __FILE__, __LINE__);
            }
            else
            {
                std::cout << e.what ();
                std::unexpected ();
            }
        }
    }
    catch (...)
    {
        if (!returnFromException)
        {
            JUCEApplication* app = JUCEApplication::getInstance ();

            if (app)
            {
                app->unhandledException (0, __FILE__, __LINE__);
            }
            else
            {
                std::unexpected ();
            }
        }
    }
    return caughtException;
}
#endif

//------------------------------------------------------------------------------

class ProtectedCall::DefaultHandler : public Handler
{
public:
    void onException (Exception const&) const
    {
        ScopedLockType lock (s_mutex);
        fatal_error ("An unhandled exception was thrown");
    }

private:
    typedef CriticalSection LockType;
    typedef CriticalSection::ScopedLockType ScopedLockType;

    static LockType s_mutex;
};

ProtectedCall::DefaultHandler::LockType ProtectedCall::DefaultHandler::s_mutex;

//------------------------------------------------------------------------------

ProtectedCall::Handler const* ProtectedCall::s_handler;

void ProtectedCall::setHandler (Handler const& handler)
{
    s_handler = &handler;
}

void ProtectedCall::call (Call& c)
{
    static DefaultHandler defaultHandler;

    Handler const* handler = s_handler;

    if (handler == nullptr)
        handler = &defaultHandler;

#if BEAST_CATCH_UNHANDLED_EXCEPTIONS
    try
    {
        c ();
    }
    catch (...)
    {
        Exception e;

        handler->onException (e);
    }

#else
    c ();

#endif
}

//------------------------------------------------------------------------------

class ProtectedCallTests : public UnitTest
{
public:
    ProtectedCallTests () : UnitTest ("ProtectedCall", "beast", runManual)
    {
    }

    void testThrow ()
    {
        throw std::runtime_error ("uncaught exception");
    }

    void runTest ()
    {
        beginTestCase ("throw");

        ProtectedCall (&ProtectedCallTests::testThrow, this);

        // If we get here then we failed
        fail ();
    }
};

static ProtectedCallTests protectedCallTests;
