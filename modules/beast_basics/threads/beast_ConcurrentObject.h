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
class ConcurrentObject : Uncopyable
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

