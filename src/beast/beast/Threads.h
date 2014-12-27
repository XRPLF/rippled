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

#ifndef BEAST_THREADS_H_INCLUDED
#define BEAST_THREADS_H_INCLUDED

#include <beast/threads/UnlockGuard.h>
#include <beast/threads/TryLockGuard.h>
#include <beast/threads/SharedLockGuard.h>
#include <beast/threads/SharedMutexAdapter.h>
#include <beast/threads/SharedData.h>
#include <beast/threads/SpinLock.h>
#include <beast/threads/Stoppable.h>
#include <beast/threads/Thread.h>
#include <beast/threads/WaitableEvent.h>
#include <beast/threads/ScopedWrapperContext.h>

#include <beast/threads/semaphore.h>

#endif
