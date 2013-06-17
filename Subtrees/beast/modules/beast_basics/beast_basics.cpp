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

/** Add this to get the @ref beast_basics module.

    @file beast_basics.cpp
    @ingroup beast_basics
*/

#include "BeastConfig.h"

#include "beast_basics.h"

#if BEAST_MSVC && _DEBUG
#include <crtdbg.h>
#endif

#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4100) // unreferenced formal parmaeter
#pragma warning (disable: 4355) // 'this' used in base member
#endif

namespace beast
{

#include "diagnostic/beast_CatchAny.cpp"
#include "diagnostic/beast_Debug.cpp"
#include "diagnostic/beast_Error.cpp"
#include "diagnostic/beast_FPUFlags.cpp"
#include "diagnostic/beast_LeakChecked.cpp"

#include "events/beast_OncePerSecond.cpp"
#include "events/beast_PerformedAtExit.cpp"

#include "math/beast_MurmurHash.cpp"

#include "threads/beast_InterruptibleThread.cpp"
#include "threads/beast_Semaphore.cpp"

#if BEAST_WINDOWS
#include "native/beast_win32_FPUFlags.cpp"
#include "native/beast_win32_Threads.cpp"

#else
#include "native/beast_posix_FPUFlags.cpp"
#include "native/beast_posix_Threads.cpp"

#endif

#if BEAST_USE_BOOST
#include "memory/beast_FifoFreeStoreWithTLS.cpp"
#else
#include "memory/beast_FifoFreeStoreWithoutTLS.cpp"
#endif
#include "memory/beast_GlobalPagedFreeStore.cpp"
#include "memory/beast_PagedFreeStore.cpp"

#include "threads/beast_CallQueue.cpp"
#include "threads/beast_ConcurrentObject.cpp"
#include "threads/beast_Listeners.cpp"
#include "threads/beast_ManualCallQueue.cpp"
#include "threads/beast_ParallelFor.cpp"
#include "threads/beast_ReadWriteMutex.cpp"
#include "threads/beast_SharedObject.cpp"
#include "threads/beast_ThreadGroup.cpp"
#include "threads/beast_ThreadWithCallQueue.cpp"

}

#if BEAST_MSVC
#pragma warning (pop)
#endif
