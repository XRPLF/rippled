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

class DynamicArrayTests : public UnitTest
{
public:
    struct T
    {
        T ()
        {
        }

        explicit T (String what)
            : msg (what)
        {
        }

        T& operator= (T const& other)
        {
            msg = other.msg;
            return *this;
        }

        String msg;
    };

    enum
    {
        numberToAssign = 1000 * 1000,
        numberToReserve = 1000 * 1000,
        numberToMutate = 12139

    };

    void testAssign ()
    {
        String s;
        s << "assign (" << String::fromNumber <int> (numberToAssign) << ")";
        beginTestCase (s);

        DynamicArray <T> v;
        v.assign (numberToAssign);

        pass ();
    }

    void testReserve ()
    {
        String s;
        s << "reserve (" << String::fromNumber <int> (numberToReserve) << ")";
        beginTestCase (s);

        DynamicArray <T> v;
        v.reserve (numberToReserve);

        v.assign (numberToReserve);

        pass ();
    }

    void testMutate ()
    {
        String s;
        DynamicArray <T> v;

        s = "push_back (" + String::fromNumber <int> (numberToMutate) + ")";
        beginTestCase (s);
        for (std::size_t i = 0; i < numberToMutate; ++i)
            v.push_back (T (String::fromNumber (i)));
        pass ();

        s = "read [] (" + String::fromNumber <int> (numberToMutate) + ")";
        beginTestCase (s);
        for (std::size_t i = 0; i < numberToMutate; ++i)
            expect (v [i].msg == String::fromNumber (i));

        s = "write [] (" + String::fromNumber <int> (numberToMutate) + ")";
        beginTestCase (s);
        for (std::size_t i = 0; i < numberToMutate; ++i)
            v [i].msg = "+" + String::fromNumber (i);
        pass ();

        s = "verify [] (" + String::fromNumber <int> (numberToMutate) + ")";
        beginTestCase (s);
        for (std::size_t i = 0; i < numberToMutate; ++i)
            expect (v [i].msg == String ("+") + String::fromNumber (i));
    }

    void testIterate ()
    {
        typedef DynamicArray <T> V;

        V v;
        for (std::size_t i = 0; i < numberToMutate; ++i)
            v.push_back (T (String::fromNumber (i)));

        {
            int step = 1;
            beginTestCase ("iterator");
            V::iterator iter;
            for (iter = v.begin (); iter + step < v.end (); iter += step)
            {
                step ++;
                V::difference_type d = iter - v.begin ();
                expect (iter->msg == String::fromNumber (d));
            }
        }

        {
            int step = 1;
            beginTestCase ("const_iterator");
            V::const_iterator iter;
            for (iter = v.begin (); iter + step < v.end (); iter += step)
            {
                step ++;
                V::difference_type d = iter - v.begin ();
                expect (iter->msg == String::fromNumber (d));
            }
        }

        {
            int step = 1;
            beginTestCase ("reverse_iterator");
            V::reverse_iterator iter;
            for (iter = v.rbegin (); iter + step < v.rend (); iter += step)
            {
                step ++;
                iter - v.rend ();
            }
            pass ();
        }

        {
            int step = 1;
            beginTestCase ("const_reverse_iterator");
            V::const_reverse_iterator iter;
            for (iter = v.crbegin (); iter + step < v.crend (); iter += step)
            {
                step ++;
                iter - v.crend ();
            }
            pass ();
        }
    }

    void runTest ()
    {
        testAssign ();
        testReserve ();
        testMutate ();
        testIterate ();
    }

    DynamicArrayTests () : UnitTest ("DynamicArray", "beast")
    {
    }
};

static DynamicArrayTests dynamicArrayTests;
