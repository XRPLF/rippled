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

#if BEAST_INCLUDE_BEASTCONFIG
#include "../../BeastConfig.h"
#endif

#include <beast/Atomic.h>
#include <beast/Arithmetic.h>
#include <beast/ByteOrder.h>

#include <beast/unit_test/suite.h>

namespace beast {

class Atomic_test : public unit_test::suite
{
public:
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

    void run()
    {
        testcase ("Misc");

        char a1[7];
        expect (numElementsInArray(a1) == 7);
        int a2[3];
        expect (numElementsInArray(a2) == 3);

        expect (ByteOrder::swap ((std::uint16_t) 0x1122) == 0x2211);
        expect (ByteOrder::swap ((std::uint32_t) 0x11223344) == 0x44332211);
        expect (ByteOrder::swap ((std::uint64_t) 0x1122334455667788LL) == 0x8877665544332211LL);

        testcase ("int");
        testInteger <int> ();

        testcase ("unsigned int");
        testInteger <unsigned int> ();

        testcase ("std::int32_t");
        testInteger <std::int32_t> ();

        testcase ("std::uint32_t");
        testInteger <std::uint32_t> ();

        testcase ("long");
        testInteger <long> ();

        testcase ("void*");
        testInteger <void*> ();

        testcase ("int*");
        testInteger <int*> ();

        testcase ("float");
        testFloat <float> ();

    #if ! BEAST_64BIT_ATOMICS_UNAVAILABLE  // 64-bit intrinsics aren't available on some old platforms
        testcase ("std::int64_t");
        testInteger <std::int64_t> ();

        testcase ("std::uint64_t");
        testInteger <std::uint64_t> ();

        testcase ("double");
        testFloat <double> ();
    #endif
    }
};

BEAST_DEFINE_TESTSUITE(Atomic,thread,beast);

}
