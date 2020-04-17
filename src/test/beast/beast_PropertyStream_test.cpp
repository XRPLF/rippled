//------------------------------------------------------------------------------
/*
This file is part of rippled: https://github.com/ripple/rippled
Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/PropertyStream.h>
namespace beast {

class PropertyStream_test : public unit_test::suite
{
public:
    using Source = PropertyStream::Source;

    void
    test_peel_name(
        std::string s,
        std::string const& expected,
        std::string const& expected_remainder)
    {
        try
        {
            std::string const peeled_name = Source::peel_name(&s);
            BEAST_EXPECT(peeled_name == expected);
            BEAST_EXPECT(s == expected_remainder);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    test_peel_leading_slash(
        std::string s,
        std::string const& expected,
        bool should_be_found)
    {
        try
        {
            bool const found(Source::peel_leading_slash(&s));
            BEAST_EXPECT(found == should_be_found);
            BEAST_EXPECT(s == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    test_peel_trailing_slashstar(
        std::string s,
        std::string const& expected_remainder,
        bool should_be_found)
    {
        try
        {
            bool const found(Source::peel_trailing_slashstar(&s));
            BEAST_EXPECT(found == should_be_found);
            BEAST_EXPECT(s == expected_remainder);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    test_find_one(Source& root, Source* expected, std::string const& name)
    {
        try
        {
            Source* source(root.find_one(name));
            BEAST_EXPECT(source == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    test_find_path(Source& root, std::string const& path, Source* expected)
    {
        try
        {
            Source* source(root.find_path(path));
            BEAST_EXPECT(source == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    test_find_one_deep(Source& root, std::string const& name, Source* expected)
    {
        try
        {
            Source* source(root.find_one_deep(name));
            BEAST_EXPECT(source == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    test_find(
        Source& root,
        std::string path,
        Source* expected,
        bool expected_star)
    {
        try
        {
            auto const result(root.find(path));
            BEAST_EXPECT(result.first == expected);
            BEAST_EXPECT(result.second == expected_star);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    run() override
    {
        Source a("a");
        Source b("b");
        Source c("c");
        Source d("d");
        Source e("e");
        Source f("f");
        Source g("g");

        //
        // a { b { d { f }, e }, c { g } }
        //

        a.add(b);
        a.add(c);
        c.add(g);
        b.add(d);
        b.add(e);
        d.add(f);

        testcase("peel_name");
        test_peel_name("a", "a", "");
        test_peel_name("foo/bar", "foo", "bar");
        test_peel_name("foo/goo/bar", "foo", "goo/bar");
        test_peel_name("", "", "");

        testcase("peel_leading_slash");
        test_peel_leading_slash("foo/", "foo/", false);
        test_peel_leading_slash("foo", "foo", false);
        test_peel_leading_slash("/foo/", "foo/", true);
        test_peel_leading_slash("/foo", "foo", true);

        testcase("peel_trailing_slashstar");
        test_peel_trailing_slashstar("/foo/goo/*", "/foo/goo", true);
        test_peel_trailing_slashstar("foo/goo/*", "foo/goo", true);
        test_peel_trailing_slashstar("/foo/goo/", "/foo/goo", false);
        test_peel_trailing_slashstar("foo/goo", "foo/goo", false);
        test_peel_trailing_slashstar("", "", false);
        test_peel_trailing_slashstar("/", "", false);
        test_peel_trailing_slashstar("/*", "", true);
        test_peel_trailing_slashstar("//", "/", false);
        test_peel_trailing_slashstar("**", "*", true);
        test_peel_trailing_slashstar("*/", "*", false);

        testcase("find_one");
        test_find_one(a, &b, "b");
        test_find_one(a, nullptr, "d");
        test_find_one(b, &e, "e");
        test_find_one(d, &f, "f");

        testcase("find_path");
        test_find_path(a, "a", nullptr);
        test_find_path(a, "e", nullptr);
        test_find_path(a, "a/b", nullptr);
        test_find_path(a, "a/b/e", nullptr);
        test_find_path(a, "b/e/g", nullptr);
        test_find_path(a, "b/e/f", nullptr);
        test_find_path(a, "b", &b);
        test_find_path(a, "b/e", &e);
        test_find_path(a, "b/d/f", &f);

        testcase("find_one_deep");
        test_find_one_deep(a, "z", nullptr);
        test_find_one_deep(a, "g", &g);
        test_find_one_deep(a, "b", &b);
        test_find_one_deep(a, "d", &d);
        test_find_one_deep(a, "f", &f);

        testcase("find");
        test_find(a, "", &a, false);
        test_find(a, "*", &a, true);
        test_find(a, "/b", &b, false);
        test_find(a, "b", &b, false);
        test_find(a, "d", &d, false);
        test_find(a, "/b*", &b, true);
        test_find(a, "b*", &b, true);
        test_find(a, "d*", &d, true);
        test_find(a, "/b/*", &b, true);
        test_find(a, "b/*", &b, true);
        test_find(a, "d/*", &d, true);
        test_find(a, "a", nullptr, false);
        test_find(a, "/d", nullptr, false);
        test_find(a, "/d*", nullptr, true);
        test_find(a, "/d/*", nullptr, true);
    }
};

BEAST_DEFINE_TESTSUITE(PropertyStream, utility, beast);
}  // namespace beast