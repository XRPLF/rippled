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

#ifndef BEAST_CATCHANY_BEASTHEADER
#define BEAST_CATCHANY_BEASTHEADER

#include "../functor/beast_Function.h"

/**
  Exception catcher.

  Executes the function and catches any exception.
  In addition to C++ exceptions, this will also catch
  any platform-specific exceptions. For example, SEH
  (Structured Exception Handling) on Windows, or POSIX
  signals if they are available.

  If returnFromException is false then a framework
  specific unhandled exception handler will be called.
  Otherwise, this function will return true if it
  catches something or else false.

  The return value approach is useful for detecting
  when outside code fails (for example, a VST plugin),
  and disabling its future use for example.

  @todo Remove dependence on the JUCEApplication object and remove beast_gui_basics.h from beast_core.cpp

  @param f The function to call.

  @param returnFromException `false` if exceptions should terminate the app.

  @return `true` if an exception was caught.

  @ingroup beast_core
*/
extern bool CatchAny (Function <void (void)> f,
                      bool returnFromException = false);

#endif
