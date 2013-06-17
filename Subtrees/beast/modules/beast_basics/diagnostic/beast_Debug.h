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

#ifndef BEAST_DEBUG_BEASTHEADER
#define BEAST_DEBUG_BEASTHEADER

// Auxiliary outines for debugging

namespace Debug
{

// Returns true if a debugger is attached, for any build.
extern bool isDebuggerAttached ();

// Breaks to the debugger if a debugger is attached.
extern void breakPoint ();

// VF: IS THIS REALLY THE RIGHT PLACE FOR THESE??

// Return only the filename portion of sourceFileName
// This hides the programmer's directory structure from end-users.
const String getFileNameFromPath (const char* sourceFileName);

// Convert a String that may contain double quotes and newlines
// into a String with double quotes escaped as \" and each
// line as a separate quoted command line argument.
String stringToCommandLine (const String& s);

// Convert a quoted and escaped command line back into a String
// that can contain newlines and double quotes.
String commandLineToString (const String& commandLine);

extern void setHeapAlwaysCheck (bool bAlwaysCheck);
extern void setHeapDelayedFree (bool bDelayedFree);
extern void setHeapReportLeaks (bool bReportLeaks);
extern void checkHeap ();

}

#endif
