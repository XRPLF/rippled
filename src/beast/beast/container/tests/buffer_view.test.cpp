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

#include <beast/unit_test/suite.h>

#include <beast/container/buffer_view.h>

#include <beast/cxx14/algorithm.h> // <algorithm>

namespace beast {

class buffer_view_test : public unit_test::suite
{
public:
    // Returns `true` if the iterator distance matches the size
    template <class FwdIt, class Size>
    static bool eq_dist (FwdIt first, FwdIt last, Size size)
    {
        auto const dist (std::distance (first, last));
        
        static_assert (std::is_signed <decltype (dist)>::value,
            "dist must be signed");
        
        if (dist < 0)
            return false;

        return static_cast <Size> (dist) == size;
    }

    // Check the contents of a buffer_view against the container
    template <class C, class T>
    void check (C const& c, buffer_view <T> v)
    {
        expect (! v.empty() || c.empty());
        expect (v.size() == c.size());
        expect (v.max_size() == v.size());
        expect (v.capacity() == v.size());

        expect (eq_dist (v.begin(), v.end(), v.size()));
        expect (eq_dist (v.cbegin(), v.cend(), v.size()));
        expect (eq_dist (v.rbegin(), v.rend(), v.size()));
        expect (eq_dist (v.crbegin(), v.crend(), v.size()));

        expect (std::equal (
            c.cbegin(), c.cend(), v.cbegin(), v.cend()));

        expect (std::equal (
            c.crbegin(), c.crend(), v.crbegin(), v.crend()));

        if (v.size() == c.size())
        {
            if (! v.empty())
            {
                expect (v.front() == c.front());
                expect (v.back() == c.back());
            }

            for (std::size_t i (0); i < v.size(); ++i)
                expect (v[i] == c[i]);
        }
    }

    //--------------------------------------------------------------------------

    // Call at() with an invalid index
    template <class V>
    void checkBadIndex (V& v,
        std::enable_if_t <
            std::is_const <typename V::value_type>::value>* = 0)
    {
        try
        {
            v.at(0);
            fail();
        }
        catch (std::out_of_range e)
        {
            pass();
        }
        catch (...)
        {
            fail();
        }
    }

    // Call at() with an invalid index
    template <class V>
    void checkBadIndex (V& v,
        std::enable_if_t <
            ! std::is_const <typename V::value_type>::value>* = 0)
    {
        try
        {
            v.at(0);
            fail();
        }
        catch (std::out_of_range e)
        {
            pass();
        }
        catch (...)
        {
            fail();
        }

        try
        {
            v.at(0) = 1;
            fail();
        }
        catch (std::out_of_range e)
        {
            pass();
        }
        catch (...)
        {
            fail();
        }
    }

    // Checks invariants for an empty buffer_view
    template <class V>
    void checkEmpty (V& v)
    {
        expect (v.empty());
        expect (v.size() == 0);
        expect (v.max_size() == v.size());
        expect (v.capacity() == v.size());
        expect (v.begin() == v.end());
        expect (v.cbegin() == v.cend());
        expect (v.begin() == v.cend());
        expect (v.rbegin() == v.rend());
        expect (v.crbegin() == v.rend());

        checkBadIndex (v);
    }

    // Test empty containers
    void testEmpty()
    {
        testcase ("empty");

        buffer_view <char> v1;
        checkEmpty (v1);

        buffer_view <char> v2;
        swap (v1, v2);
        checkEmpty (v1);
        checkEmpty (v2);

        buffer_view <char const> v3 (v2);
        checkEmpty (v3);
    }

    //--------------------------------------------------------------------------

    // Construct const views from a container
    template <class C>
    void testConstructConst (C const& c)
    {
        typedef buffer_view <std::add_const_t <
            typename C::value_type>> V;

        {
            // construct from container
            V v (c);
            check (c, v);

            // construct from buffer_view
            V v2 (v);
            check (c, v2);
        }

        if (! c.empty())
        {
            {
                // construct from const pointer range
                V v (&c.front(), &c.back()+1);
                check (c, v);

                // construct from pointer and size
                V v2 (&c.front(), c.size());
                check (v, v2);
            }

            {
                // construct from non const pointer range
                C cp (c);
                V v (&cp.front(), &cp.back()+1);
                check (cp, v);

                // construct from pointer and size
                V v2 (&cp.front(), cp.size());
                check (v, v2);

                // construct from data and size
                V v3 (v2.data(), v2.size());
                check (c, v3);
            }
        }
    }

    // Construct view from a container
    template <class C>
    void testConstruct (C const& c)
    {
        static_assert (! std::is_const <typename C::value_type>::value,
            "Container value_type cannot be const");

        testConstructConst (c);

        typedef buffer_view <typename C::value_type> V;

        C cp (c);
        V v (cp);
        check (cp, v);

        std::reverse (v.begin(), v.end());
        check (cp, v);

        expect (std::equal (v.rbegin(), v.rend(),
            c.begin(), c.end()));
    }

    void testConstruct()
    {
        testcase ("std::vector <char>");
        testConstruct (
            std::vector <char> ({'h', 'e', 'l', 'l', 'o'}));

        testcase ("std::string <char>");
        testConstruct (
            std::basic_string <char> ("hello"));
    }

    //--------------------------------------------------------------------------

    void testCoerce()
    {
        testcase ("coerce");

        std::string const s ("hello");
        const_buffer_view <unsigned char> v (s);

        pass();
    }

    //--------------------------------------------------------------------------

    void testAssign()
    {
        testcase ("testAssign");
        std::vector <int> v1({1, 2, 3});
        buffer_view<int> r1(v1);
        std::vector <int> v2({4, 5, 6, 7});
        buffer_view<int> r2(v2);
        r1 = r2;
        expect (std::equal (r1.begin(), r1.end(), v2.begin(), v2.end()));
    }

    //--------------------------------------------------------------------------

    static_assert (std::is_constructible <buffer_view <int>,
        std::vector <int>&>::value, "");

    static_assert (!std::is_constructible <buffer_view <int>,
        std::vector <int> const&>::value, "");

    static_assert (std::is_constructible <buffer_view <int const>,
        std::vector <int>&>::value, "");

    static_assert (std::is_constructible <buffer_view <int const>,
        std::vector <int> const&>::value, "");

    static_assert (std::is_nothrow_default_constructible <
        buffer_view <int>>::value, "");

    static_assert (std::is_nothrow_destructible <
        buffer_view <int>>::value, "");

    static_assert (std::is_nothrow_copy_constructible <
        buffer_view <int>>::value, "");

    static_assert (std::is_nothrow_copy_assignable <
        buffer_view<int>>::value, "");

    static_assert (std::is_nothrow_move_constructible <
        buffer_view <int>>::value, "");

    static_assert (std::is_nothrow_move_assignable <
        buffer_view <int>>::value, "");

    void run()
    {
        testEmpty();
        testConstruct();
        testCoerce();
        testAssign();
    }
};

BEAST_DEFINE_TESTSUITE(buffer_view,container,beast);

}
