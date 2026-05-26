#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/PropertyStream.h>

#include <string>

namespace beast {

class PropertyStream_test : public unit_test::Suite
{
public:
    using Source = PropertyStream::Source;

    void
    testPeelName(std::string s, std::string const& expected, std::string const& expectedRemainder)
    {
        try
        {
            std::string const peeledName = Source::peelName(&s);
            BEAST_EXPECT(peeledName == expected);
            BEAST_EXPECT(s == expectedRemainder);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    testPeelLeadingSlash(std::string s, std::string const& expected, bool shouldBeFound)
    {
        try
        {
            bool const found(Source::peelLeadingSlash(&s));
            BEAST_EXPECT(found == shouldBeFound);
            BEAST_EXPECT(s == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    testPeelTrailingSlashstar(
        std::string s,
        std::string const& expectedRemainder,
        bool shouldBeFound)
    {
        try
        {
            bool const found(Source::peelTrailingSlashstar(&s));
            BEAST_EXPECT(found == shouldBeFound);
            BEAST_EXPECT(s == expectedRemainder);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    testFindOne(Source& root, Source* expected, std::string const& name)
    {
        try
        {
            Source const* source(root.findOne(name));
            BEAST_EXPECT(source == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    testFindPath(Source& root, std::string const& path, Source* expected)
    {
        try
        {
            Source const* source(root.findPath(path));
            BEAST_EXPECT(source == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    testFindOneDeep(Source& root, std::string const& name, Source* expected)
    {
        try
        {
            Source const* source(root.findOneDeep(name));
            BEAST_EXPECT(source == expected);
        }
        catch (...)
        {
            fail("unhandled exception");
            ;
        }
    }

    void
    testFind(Source& root, std::string path, Source* expected, bool expectedStar)
    {
        try
        {
            auto const result(root.find(path));
            BEAST_EXPECT(result.first == expected);
            BEAST_EXPECT(result.second == expectedStar);
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
        testPeelName("a", "a", "");
        testPeelName("foo/bar", "foo", "bar");
        testPeelName("foo/goo/bar", "foo", "goo/bar");
        testPeelName("", "", "");

        testcase("peel_leading_slash");
        testPeelLeadingSlash("foo/", "foo/", false);
        testPeelLeadingSlash("foo", "foo", false);
        testPeelLeadingSlash("/foo/", "foo/", true);
        testPeelLeadingSlash("/foo", "foo", true);

        testcase("peel_trailing_slashstar");
        testPeelTrailingSlashstar("/foo/goo/*", "/foo/goo", true);
        testPeelTrailingSlashstar("foo/goo/*", "foo/goo", true);
        testPeelTrailingSlashstar("/foo/goo/", "/foo/goo", false);
        testPeelTrailingSlashstar("foo/goo", "foo/goo", false);
        testPeelTrailingSlashstar("", "", false);
        testPeelTrailingSlashstar("/", "", false);
        testPeelTrailingSlashstar("/*", "", true);
        testPeelTrailingSlashstar("//", "/", false);
        testPeelTrailingSlashstar("**", "*", true);
        testPeelTrailingSlashstar("*/", "*", false);

        testcase("find_one");
        testFindOne(a, &b, "b");
        testFindOne(a, nullptr, "d");
        testFindOne(b, &e, "e");
        testFindOne(d, &f, "f");

        testcase("find_path");
        testFindPath(a, "a", nullptr);
        testFindPath(a, "e", nullptr);
        testFindPath(a, "a/b", nullptr);
        testFindPath(a, "a/b/e", nullptr);
        testFindPath(a, "b/e/g", nullptr);
        testFindPath(a, "b/e/f", nullptr);
        testFindPath(a, "b", &b);
        testFindPath(a, "b/e", &e);
        testFindPath(a, "b/d/f", &f);

        testcase("find_one_deep");
        testFindOneDeep(a, "z", nullptr);
        testFindOneDeep(a, "g", &g);
        testFindOneDeep(a, "b", &b);
        testFindOneDeep(a, "d", &d);
        testFindOneDeep(a, "f", &f);

        testcase("find");
        testFind(a, "", &a, false);
        testFind(a, "*", &a, true);
        testFind(a, "/b", &b, false);
        testFind(a, "b", &b, false);
        testFind(a, "d", &d, false);
        testFind(a, "/b*", &b, true);
        testFind(a, "b*", &b, true);
        testFind(a, "d*", &d, true);
        testFind(a, "/b/*", &b, true);
        testFind(a, "b/*", &b, true);
        testFind(a, "d/*", &d, true);
        testFind(a, "a", nullptr, false);
        testFind(a, "/d", nullptr, false);
        testFind(a, "/d*", nullptr, true);
        testFind(a, "/d/*", nullptr, true);
    }
};

BEAST_DEFINE_TESTSUITE(PropertyStream, beast, beast);
}  // namespace beast
