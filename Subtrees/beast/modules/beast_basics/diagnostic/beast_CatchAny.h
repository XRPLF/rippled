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
