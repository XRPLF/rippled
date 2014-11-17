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

#ifndef BEAST_UTILITY_DEBUG_H_INCLUDED
#define BEAST_UTILITY_DEBUG_H_INCLUDED

#include <beast/strings/String.h>
    
namespace beast {

// Auxiliary outines for debugging

namespace Debug
{

/** Break to debugger if a debugger is attached to a debug build.

    Does nothing if no debugger is attached, or the build is not a debug build.
*/
extern void breakPoint ();

/** Given a file and line number this formats a suitable string.
    Usually you will pass __FILE__ and __LINE__ here.
*/
String getSourceLocation (char const* fileName, int lineNumber,
                          int numberOfParents = 0);

/** Retrieve the file name from a full path.
    The nubmer of parents can be chosen
*/
String getFileNameFromPath (const char* sourceFileName, int numberOfParents = 0);

//
// These control the MSVC C Runtime Debug heap.
//
// The calls currently do nothing on other platforms.
//

/** Calls checkHeap() at every allocation and deallocation.
*/
extern void setAlwaysCheckHeap (bool bAlwaysCheck);

/** Keep freed memory blocks in the heap's linked list, assign them the
    _FREE_BLOCK type, and fill them with the byte value 0xDD.
*/
extern void setHeapDelayedFree (bool bDelayedFree);

/** Perform automatic leak checking at program exit through a call to
    dumpMemoryLeaks() and generate an error report if the application
    failed to free all the memory it allocated.
*/
extern void setHeapReportLeaks (bool bReportLeaks);

/** Report all memory blocks which have not been freed.
*/
extern void reportLeaks ();

/** Confirms the integrity of the memory blocks allocated in the
    debug heap (debug version only.
*/
extern void checkHeap ();

}

}

#endif
