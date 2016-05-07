//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/static_string.hpp>

#include <beast/unit_test/suite.hpp>

namespace beast {

class static_string_test : public beast::unit_test::suite
{
public:
    void testMembers()
    {
        using str1 = static_string<1>;
        using str2 = static_string<2>;
        {
            str1 s1;
            expect(s1 == "");
            expect(s1.empty());
            expect(s1.size() == 0);
            expect(s1.max_size() == 1);
            expect(s1.capacity() == 1);
            expect(s1.begin() == s1.end());
            expect(s1.cbegin() == s1.cend());
            expect(s1.rbegin() == s1.rend());
            expect(s1.crbegin() == s1.crend());
            try
            {
                expect(s1.at(0) == 0);
                fail();
            }
            catch(std::exception const&)
            {
                pass();
            }
            expect(s1.data()[0] == 0);
            expect(*s1.c_str() == 0);
            expect(std::distance(s1.begin(), s1.end()) == 0);
            expect(std::distance(s1.cbegin(), s1.cend()) == 0);
            expect(std::distance(s1.rbegin(), s1.rend()) == 0);
            expect(std::distance(s1.crbegin(), s1.crend()) == 0);
            expect(s1.compare(s1) == 0);
            expect(s1.to_string() == std::string{});
        }
        {
            str1 const s1;
            expect(s1 == "");
            expect(s1.empty());
            expect(s1.size() == 0);
            expect(s1.max_size() == 1);
            expect(s1.capacity() == 1);
            expect(s1.begin() == s1.end());
            expect(s1.cbegin() == s1.cend());
            expect(s1.rbegin() == s1.rend());
            expect(s1.crbegin() == s1.crend());
            try
            {
                expect(s1.at(0) == 0);
                fail();
            }
            catch(std::exception const&)
            {
                pass();
            }
            expect(s1.data()[0] == 0);
            expect(*s1.c_str() == 0);
            expect(std::distance(s1.begin(), s1.end()) == 0);
            expect(std::distance(s1.cbegin(), s1.cend()) == 0);
            expect(std::distance(s1.rbegin(), s1.rend()) == 0);
            expect(std::distance(s1.crbegin(), s1.crend()) == 0);
            expect(s1.compare(s1) == 0);
            expect(s1.to_string() == std::string{});
        }
        {
            str1 s1;
            str1 s2("x");
            expect(s2 == "x");
            expect(s2[0] == 'x');
            expect(s2.at(0) == 'x');
            expect(s2.front() == 'x');
            expect(s2.back() == 'x');
            str1 const s3(s2);
            expect(s3 == "x");
            expect(s3[0] == 'x');
            expect(s3.at(0) == 'x');
            expect(s3.front() == 'x');
            expect(s3.back() == 'x');
            s2 = "y";
            expect(s2 == "y");
            expect(s3 == "x");
            s1 = s2;
            expect(s1 == "y");
            s1.clear();
            expect(s1.empty());
            expect(s1.size() == 0);
        }
        {
            str2 s1("x");
            str1 s2(s1);
            expect(s2 == "x");
            str1 s3;
            s3 = s2;
            expect(s3 == "x");
            s1 = "xy";
            expect(s1.size() == 2);
            expect(s1[0] == 'x');
            expect(s1[1] == 'y');
            expect(s1.at(0) == 'x');
            expect(s1.at(1) == 'y');
            expect(s1.front() == 'x');
            expect(s1.back() == 'y');
            auto const s4 = s1;
            expect(s4[0] == 'x');
            expect(s4[1] == 'y');
            expect(s4.at(0) == 'x');
            expect(s4.at(1) == 'y');
            expect(s4.front() == 'x');
            expect(s4.back() == 'y');
            try
            {
                s3 = s1;
                fail();
            }
            catch(std::exception const&)
            {
                pass();
            }
            try
            {
                str1 s5(s1);
                fail();
            }
            catch(std::exception const&)
            {
                pass();
            }
        }
        {
            str1 s1("x");
            str2 s2;
            s2 = s1;
            try
            {
                s1.resize(2);
                fail();
            }
            catch(std::length_error const&)
            {
                pass();
            }
        }
        pass();
    }

    void testCompare()
    {
        using str1 = static_string<1>;
        using str2 = static_string<2>;
        {
            str1 s1;
            str2 s2;
            s1 = "1";
            s2 = "22";
            expect(s1.compare(s2) < 0);
            expect(s2.compare(s1) > 0);
            expect(s1 < "10");
            expect(s2 > "1");
            expect("10" > s1);
            expect("1" < s2);
            expect(s1 < "20");
            expect(s2 > "1");
            expect(s2 > "2");
        }
        {
            str2 s1("x");
            str2 s2("x");
            expect(s1 == s2);
            expect(s1 <= s2);
            expect(s1 >= s2);
            expect(! (s1 < s2));
            expect(! (s1 > s2));
            expect(! (s1 != s2));
        }
        {
            str1 s1("x");
            str2 s2("x");
            expect(s1 == s2);
            expect(s1 <= s2);
            expect(s1 >= s2);
            expect(! (s1 < s2));
            expect(! (s1 > s2));
            expect(! (s1 != s2));
        }
        {
            str2 s("x");
            expect(s == "x");
            expect(s <= "x");
            expect(s >= "x");
            expect(! (s < "x"));
            expect(! (s > "x"));
            expect(! (s != "x"));
            expect("x" == s);
            expect("x" <= s);
            expect("x" >= s);
            expect(! ("x" < s));
            expect(! ("x" > s));
            expect(! ("x" != s));
        }
        {
            str2 s("x");
            expect(s <= "y");
            expect(s < "y");
            expect(s != "y");
            expect(! (s == "y"));
            expect(! (s >= "y"));
            expect(! (s > "x"));
            expect("y" >= s);
            expect("y" > s);
            expect("y" != s);
            expect(! ("y" == s));
            expect(! ("y" <= s));
            expect(! ("y" < s));
        }
        {
            str1 s1("x");
            str2 s2("y");
            expect(s1 <= s2);
            expect(s1 < s2);
            expect(s1 != s2);
            expect(! (s1 == s2));
            expect(! (s1 >= s2));
            expect(! (s1 > s2));
        }
        {
            str1 s1("x");
            str2 s2("xx");
            expect(s1 < s2);
            expect(s2 > s1);
        }
        {
            str1 s1("x");
            str2 s2("yy");
            expect(s1 < s2);
            expect(s2 > s1);
        }
    }

    void run() override
    {
        testMembers();
        testCompare();
    }
};

BEAST_DEFINE_TESTSUITE(static_string,core,beast);

} // beast
