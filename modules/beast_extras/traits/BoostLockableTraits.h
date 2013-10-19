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

#ifndef BEAST_BOOSTLOCKABLERAITS_H_INCLUDED
#define BEAST_BOOSTLOCKABLERAITS_H_INCLUDED

// Adds beast specializations for boost lockables.

// beast forward declarations

namespace beast
{

class CriticalSection;

template <typename Mutex>
class TrackedMutex;

template <typename Mutex>
class UntrackedMutex;

}

//------------------------------------------------------------------------------

// boost Mutex concepts
//
// http://www.boost.org/doc/libs/1_54_0/doc/html/thread/synchronization.html#thread.synchronization.mutex_concepts

namespace boost
{

namespace sync
{

//------------------------------------------------------------------------------
//
//  CriticalSection
//

// BasicLockable
//
template <>
struct is_basic_lockable <beast::CriticalSection>
    : public boost::true_type { };

// Lockable
//
template <>
struct is_lockable <beast::CriticalSection>
    : public boost::true_type { };

// RecursiveLockable
//
template <>
struct is_recursive_mutex_sur_parole <beast::CriticalSection>
    : public boost::true_type { };

//------------------------------------------------------------------------------
//
//  TrackedMutex <>
//

// BasicLockable
//
template <class Mutex>
class is_basic_lockable <beast::TrackedMutex <Mutex> >
    : public boost::sync::is_basic_lockable <Mutex> { };

// Lockable
//
template <class Mutex>
class is_lockable <beast::TrackedMutex <Mutex> >
    : public boost::sync::is_lockable <Mutex> { };

// RecursiveLockable
//
template <class Mutex>
struct is_recursive_mutex_sur_parole <beast::TrackedMutex <Mutex> >
    : public boost::true_type { };

//------------------------------------------------------------------------------
//
//  UntrackedMutex <>
//

// BasicLockable
//
template <class Mutex>
class is_basic_lockable <beast::UntrackedMutex <Mutex> >
    : public boost::sync::is_basic_lockable <Mutex> { };

// Lockable
//
template <class Mutex>
class is_lockable <beast::UntrackedMutex <Mutex> >
    : public boost::sync::is_lockable <Mutex> { };

// RecursiveLockable
//
template <class Mutex>
struct is_recursive_mutex_sur_parole <beast::UntrackedMutex <Mutex> >
    : public boost::true_type { };

}

}

#endif
