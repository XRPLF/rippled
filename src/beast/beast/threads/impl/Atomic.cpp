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

#include "../../../modules/beast_core/beast_core.h" // for UnitTest

namespace beast {

class AtomicTests : public UnitTest
{
public:
    AtomicTests() : UnitTest ("Atomic", "beast") {}

    template <typename Type>
    void testFloat ()
    {
        Atomic<Type> a, b;
        a = (Type) 21;
        memoryBarrier();

        /*  These are some simple test cases to check the atomics - let me know
            if any of these assertions fail on your system!
        */
        expect (a.get() == (Type) 21);
        expect (a.compareAndSetValue ((Type) 100, (Type) 50) == (Type) 21);
        expect (a.get() == (Type) 21);
        expect (a.compareAndSetValue ((Type) 101, a.get()) == (Type) 21);
        expect (a.get() == (Type) 101);
        expect (! a.compareAndSetBool ((Type) 300, (Type) 200));
        expect (a.get() == (Type) 101);
        expect (a.compareAndSetBool ((Type) 200, a.get()));
        expect (a.get() == (Type) 200);

        expect (a.exchange ((Type) 300) == (Type) 200);
        expect (a.get() == (Type) 300);

        b = a;
        expect (b.get() == a.get());
    }

    template <typename Type>
    void testInteger ()
    {
        Atomic<Type> a, b;
        a.set ((Type) 10);
        expect (a.value == (Type) 10);
        expect (a.get() == (Type) 10);
        a += (Type) 15;
        expect (a.get() == (Type) 25);
        memoryBarrier();
        a -= (Type) 5;
        expect (a.get() == (Type) 20);
        expect (++a == (Type) 21);
        ++a;
        expect (--a == (Type) 21);
        expect (a.get() == (Type) 21);
        memoryBarrier();

        testFloat <Type> ();
    }

    void testVoidStar ()
    {
        Atomic<void *> a;

        void *testValue = reinterpret_cast<void*>(10);
        
        a.set (testValue);
        expect (a.value == testValue);
        expect (a.get() == testValue);
        memoryBarrier();
          
        testFloat <void*> ();
    }

    void runTest()
    {
        beginTestCase ("Misc");

        char a1[7];
        expect (numElementsInArray(a1) == 7);
        int a2[3];
        expect (numElementsInArray(a2) == 3);

        expect (ByteOrder::swap ((uint16) 0x1122) == 0x2211);
        expect (ByteOrder::swap ((uint32) 0x11223344) == 0x44332211);
        expect (ByteOrder::swap ((uint64) literal64bit (0x1122334455667788)) == literal64bit (0x8877665544332211));

        beginTestCase ("int");
        testInteger <int> ();

        beginTestCase ("unsigned int");
        testInteger <unsigned int> ();

        beginTestCase ("int32");
        testInteger <int32> ();

        beginTestCase ("uint32");
        testInteger <uint32> ();

        beginTestCase ("long");
        testInteger <long> ();

        beginTestCase ("void*");
        testVoidStar ();

        beginTestCase ("int*");
        testInteger <int*> ();

        beginTestCase ("float");
        testFloat <float> ();

    #if ! BEAST_64BIT_ATOMICS_UNAVAILABLE  // 64-bit intrinsics aren't available on some old platforms
        beginTestCase ("int64");
        testInteger <int64> ();

        beginTestCase ("uint64");
        testInteger <uint64> ();

        beginTestCase ("double");
        testFloat <double> ();
    #endif
    }
};

static AtomicTests atomicTests;

}
