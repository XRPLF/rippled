//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/handler_alloc.hpp>

#include <beast/unit_test/suite.hpp>
#include <list>
#include <vector>

namespace beast {

class handler_alloc_test : public beast::unit_test::suite
{
public:
    struct handler
    {
        void
        operator()() const
        {
        }
    };

    // https://github.com/vinniefalco/Beast/issues/432
    void
    testRegression432()
    {
        handler h;
        handler_alloc<int, handler> a{h};
        std::list<int, handler_alloc<int, handler>> v{a};
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);
    }

    void
    run() override
    {
        testRegression432();

        handler h;
        handler h2;
        handler_alloc<char, handler> a1{h};
        handler_alloc<char, handler> a2{h2};
        BEAST_EXPECT(a2 == a1);
        auto a3 = a1;
        BEAST_EXPECT(a3 == a1);
        {
            std::vector<char,
                handler_alloc<char, handler>> v(a1);
            v.reserve(32);
            v.resize(10);
        }
    }
};

BEAST_DEFINE_TESTSUITE(handler_alloc,core,beast);

} // beast

