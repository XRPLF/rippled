//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_LEAKEDOBJECTDETECTOR_BEASTHEADER
#define BEAST_LEAKEDOBJECTDETECTOR_BEASTHEADER

#include "../text/beast_String.h"
#include "beast_Atomic.h"


//==============================================================================
/**
    Embedding an instance of this class inside another class can be used as a low-overhead
    way of detecting leaked instances.

    This class keeps an internal static count of the number of instances that are
    active, so that when the app is shutdown and the static destructors are called,
    it can check whether there are any left-over instances that may have been leaked.

    To use it, use the BEAST_LEAK_DETECTOR macro as a simple way to put one in your
    class declaration. Have a look through the beast codebase for examples, it's used
    in most of the classes.
*/
template <class OwnerClass>
class LeakedObjectDetector
{
public:
    //==============================================================================
    LeakedObjectDetector() noexcept                                 { ++(getCounter().numObjects); }
    LeakedObjectDetector (const LeakedObjectDetector&) noexcept     { ++(getCounter().numObjects); }

    ~LeakedObjectDetector()
    {
        if (--(getCounter().numObjects) < 0)
        {
            DBG ("*** Dangling pointer deletion! Class: " << getLeakedObjectClassName());

            /** If you hit this, then you've managed to delete more instances of this class than you've
                created.. That indicates that you're deleting some dangling pointers.

                Note that although this assertion will have been triggered during a destructor, it might
                not be this particular deletion that's at fault - the incorrect one may have happened
                at an earlier point in the program, and simply not been detected until now.

                Most errors like this are caused by using old-fashioned, non-RAII techniques for
                your object management. Tut, tut. Always, always use ScopedPointers, OwnedArrays,
                ReferenceCountedObjects, etc, and avoid the 'delete' operator at all costs!
            */
            bassertfalse;
        }
    }

private:
    //==============================================================================
    class LeakCounter
    {
    public:
        LeakCounter() noexcept {}

        ~LeakCounter()
        {
            if (numObjects.value > 0)
            {
                DBG ("*** Leaked objects detected: " << numObjects.value << " instance(s) of class " << getLeakedObjectClassName());

                /** If you hit this, then you've leaked one or more objects of the type specified by
                    the 'OwnerClass' template parameter - the name should have been printed by the line above.

                    If you're leaking, it's probably because you're using old-fashioned, non-RAII techniques for
                    your object management. Tut, tut. Always, always use ScopedPointers, OwnedArrays,
                    ReferenceCountedObjects, etc, and avoid the 'delete' operator at all costs!
                */
                bassertfalse;
            }
        }

        Atomic<int> numObjects;
    };

    static const char* getLeakedObjectClassName()
    {
        return OwnerClass::getLeakedObjectClassName();
    }

    static LeakCounter& getCounter() noexcept
    {
        static LeakCounter counter;
        return counter;
    }
};

//==============================================================================
#if DOXYGEN || ! defined (BEAST_LEAK_DETECTOR)
 #if (DOXYGEN || BEAST_CHECK_MEMORY_LEAKS)
  /** This macro lets you embed a leak-detecting object inside a class.

      To use it, simply declare a BEAST_LEAK_DETECTOR(YourClassName) inside a private section
      of the class declaration. E.g.

      @code
      class MyClass
      {
      public:
          MyClass();
          void blahBlah();

      private:
          BEAST_LEAK_DETECTOR (MyClass)
      };
      @endcode

      @see BEAST_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR, LeakedObjectDetector
  */
  #define BEAST_LEAK_DETECTOR(OwnerClass) \
        friend class beast::LeakedObjectDetector<OwnerClass>; \
        static const char* getLeakedObjectClassName() noexcept { return #OwnerClass; } \
        beast::LeakedObjectDetector<OwnerClass> BEAST_JOIN_MACRO (leakDetector, __LINE__);
 #else
  #define BEAST_LEAK_DETECTOR(OwnerClass)
 #endif
#endif


#endif   // BEAST_LEAKEDOBJECTDETECTOR_BEASTHEADER
