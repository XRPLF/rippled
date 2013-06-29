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

#ifndef BEAST_CONCURRENTOBJECT_BEASTHEADER
#define BEAST_CONCURRENTOBJECT_BEASTHEADER

/*============================================================================*/
/**
  A reference counted object with overridable destroy behavior.

  This is a reference counted object compatible with
  ReferenceCountedObjectPtr. When the last reference is removed, the
  object is queued for deletion on a separate, provided thread. On
  program exit the thread will clean itself up - no other action is
  required.

  This class is useful for offloading the deletion work of "deep" objects
  shared by multiple threads: objects containing complex members, or a
  hierarchy of allocated structures. For example, a ValueTree. The problem
  of performing heavyweight memory or cleanup operations from either an
  AudioIODeviceCallback or the message thread is avoided.

  The deletion behavior can be overriden by providing a replacement
  for destroyConcurrentObject().

  @ingroup beast_concurrent
*/
class BEAST_API ConcurrentObject : Uncopyable
{
public:
    inline void incReferenceCount () noexcept
    {
        m_refs.addref ();
    }

    inline void decReferenceCount () noexcept
    {
        if (m_refs.release ())
            destroyConcurrentObject ();
    }

protected:
    ConcurrentObject ();

    virtual ~ConcurrentObject ();

    /** Delete the object.

        This function is called when the reference count drops to zero. The
        default implementation performs the delete on a separate, provided thread
        that cleans up after itself on exit.
    */
    virtual void destroyConcurrentObject ();

protected:
    class Deleter;

private:
    AtomicCounter m_refs;
};

#endif

