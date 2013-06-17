/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

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
class ScopedPlatformExceptionCatcher : Uncopyable
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

#pragma message(BEAST_LOC_"Missing class ScopedPlatformExceptionCatcher")

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

bool CatchAny (Function <void (void)> f, bool returnFromException)
{
    bool caughtException = true; // assume the worst

#if 0
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
#endif
    return caughtException;
}
