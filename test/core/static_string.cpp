//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/static_string.hpp>

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
            BEAST_EXPECT(s1 == "");
            BEAST_EXPECT(s1.empty());
            BEAST_EXPECT(s1.size() == 0);
            BEAST_EXPECT(s1.max_size() == 1);
            BEAST_EXPECT(s1.capacity() == 1);
            BEAST_EXPECT(s1.begin() == s1.end());
            BEAST_EXPECT(s1.cbegin() == s1.cend());
            BEAST_EXPECT(s1.rbegin() == s1.rend());
            BEAST_EXPECT(s1.crbegin() == s1.crend());
            try
            {
                BEAST_EXPECT(s1.at(0) == 0);
                fail();
            }
            catch(std::exception const&)
            {
                pass();
            }
            BEAST_EXPECT(s1.data()[0] == 0);
            BEAST_EXPECT(*s1.c_str() == 0);
            BEAST_EXPECT(std::distance(s1.begin(), s1.end()) == 0);
            BEAST_EXPECT(std::distance(s1.cbegin(), s1.cend()) == 0);
            BEAST_EXPECT(std::distance(s1.rbegin(), s1.rend()) == 0);
            BEAST_EXPECT(std::distance(s1.crbegin(), s1.crend()) == 0);
            BEAST_EXPECT(s1.compare(s1) == 0);
            BEAST_EXPECT(s1.to_string() == std::string{});
        }
        {
            str1 const s1;
            BEAST_EXPECT(s1 == "");
            BEAST_EXPECT(s1.empty());
            BEAST_EXPECT(s1.size() == 0);
            BEAST_EXPECT(s1.max_size() == 1);
            BEAST_EXPECT(s1.capacity() == 1);
            BEAST_EXPECT(s1.begin() == s1.end());
            BEAST_EXPECT(s1.cbegin() == s1.cend());
            BEAST_EXPECT(s1.rbegin() == s1.rend());
            BEAST_EXPECT(s1.crbegin() == s1.crend());
            try
            {
                BEAST_EXPECT(s1.at(0) == 0);
                fail();
            }
            catch(std::exception const&)
            {
                pass();
            }
            BEAST_EXPECT(s1.data()[0] == 0);
            BEAST_EXPECT(*s1.c_str() == 0);
            BEAST_EXPECT(std::distance(s1.begin(), s1.end()) == 0);
            BEAST_EXPECT(std::distance(s1.cbegin(), s1.cend()) == 0);
            BEAST_EXPECT(std::distance(s1.rbegin(), s1.rend()) == 0);
            BEAST_EXPECT(std::distance(s1.crbegin(), s1.crend()) == 0);
            BEAST_EXPECT(s1.compare(s1) == 0);
            BEAST_EXPECT(s1.to_string() == std::string{});
        }
        {
            str1 s1;
            str1 s2("x");
            BEAST_EXPECT(s2 == "x");
            BEAST_EXPECT(s2[0] == 'x');
            BEAST_EXPECT(s2.at(0) == 'x');
            BEAST_EXPECT(s2.front() == 'x');
            BEAST_EXPECT(s2.back() == 'x');
            str1 const s3(s2);
            BEAST_EXPECT(s3 == "x");
            BEAST_EXPECT(s3[0] == 'x');
            BEAST_EXPECT(s3.at(0) == 'x');
            BEAST_EXPECT(s3.front() == 'x');
            BEAST_EXPECT(s3.back() == 'x');
            s2 = "y";
            BEAST_EXPECT(s2 == "y");
            BEAST_EXPECT(s3 == "x");
            s1 = s2;
            BEAST_EXPECT(s1 == "y");
            s1.clear();
            BEAST_EXPECT(s1.empty());
            BEAST_EXPECT(s1.size() == 0);
        }
        {
            str2 s1("x");
            str1 s2(s1);
            BEAST_EXPECT(s2 == "x");
            str1 s3;
            s3 = s2;
            BEAST_EXPECT(s3 == "x");
            s1 = "xy";
            BEAST_EXPECT(s1.size() == 2);
            BEAST_EXPECT(s1[0] == 'x');
            BEAST_EXPECT(s1[1] == 'y');
            BEAST_EXPECT(s1.at(0) == 'x');
            BEAST_EXPECT(s1.at(1) == 'y');
            BEAST_EXPECT(s1.front() == 'x');
            BEAST_EXPECT(s1.back() == 'y');
            auto const s4 = s1;
            BEAST_EXPECT(s4[0] == 'x');
            BEAST_EXPECT(s4[1] == 'y');
            BEAST_EXPECT(s4.at(0) == 'x');
            BEAST_EXPECT(s4.at(1) == 'y');
            BEAST_EXPECT(s4.front() == 'x');
            BEAST_EXPECT(s4.back() == 'y');
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
            BEAST_EXPECT(s1.compare(s2) < 0);
            BEAST_EXPECT(s2.compare(s1) > 0);
            BEAST_EXPECT(s1 < "10");
            BEAST_EXPECT(s2 > "1");
            BEAST_EXPECT("10" > s1);
            BEAST_EXPECT("1" < s2);
            BEAST_EXPECT(s1 < "20");
            BEAST_EXPECT(s2 > "1");
            BEAST_EXPECT(s2 > "2");
        }
        {
            str2 s1("x");
            str2 s2("x");
            BEAST_EXPECT(s1 == s2);
            BEAST_EXPECT(s1 <= s2);
            BEAST_EXPECT(s1 >= s2);
            BEAST_EXPECT(! (s1 < s2));
            BEAST_EXPECT(! (s1 > s2));
            BEAST_EXPECT(! (s1 != s2));
        }
        {
            str1 s1("x");
            str2 s2("x");
            BEAST_EXPECT(s1 == s2);
            BEAST_EXPECT(s1 <= s2);
            BEAST_EXPECT(s1 >= s2);
            BEAST_EXPECT(! (s1 < s2));
            BEAST_EXPECT(! (s1 > s2));
            BEAST_EXPECT(! (s1 != s2));
        }
        {
            str2 s("x");
            BEAST_EXPECT(s == "x");
            BEAST_EXPECT(s <= "x");
            BEAST_EXPECT(s >= "x");
            BEAST_EXPECT(! (s < "x"));
            BEAST_EXPECT(! (s > "x"));
            BEAST_EXPECT(! (s != "x"));
            BEAST_EXPECT("x" == s);
            BEAST_EXPECT("x" <= s);
            BEAST_EXPECT("x" >= s);
            BEAST_EXPECT(! ("x" < s));
            BEAST_EXPECT(! ("x" > s));
            BEAST_EXPECT(! ("x" != s));
        }
        {
            str2 s("x");
            BEAST_EXPECT(s <= "y");
            BEAST_EXPECT(s < "y");
            BEAST_EXPECT(s != "y");
            BEAST_EXPECT(! (s == "y"));
            BEAST_EXPECT(! (s >= "y"));
            BEAST_EXPECT(! (s > "x"));
            BEAST_EXPECT("y" >= s);
            BEAST_EXPECT("y" > s);
            BEAST_EXPECT("y" != s);
            BEAST_EXPECT(! ("y" == s));
            BEAST_EXPECT(! ("y" <= s));
            BEAST_EXPECT(! ("y" < s));
        }
        {
            str1 s1("x");
            str2 s2("y");
            BEAST_EXPECT(s1 <= s2);
            BEAST_EXPECT(s1 < s2);
            BEAST_EXPECT(s1 != s2);
            BEAST_EXPECT(! (s1 == s2));
            BEAST_EXPECT(! (s1 >= s2));
            BEAST_EXPECT(! (s1 > s2));
        }
        {
            str1 s1("x");
            str2 s2("xx");
            BEAST_EXPECT(s1 < s2);
            BEAST_EXPECT(s2 > s1);
        }
        {
            str1 s1("x");
            str2 s2("yy");
            BEAST_EXPECT(s1 < s2);
            BEAST_EXPECT(s2 > s1);
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
