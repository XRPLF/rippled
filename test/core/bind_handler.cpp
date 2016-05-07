//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/bind_handler.hpp>

#include <beast/unit_test/suite.hpp>
#include <functional>

namespace beast {

class bind_handler_test : public unit_test::suite
{
public:
    static void foo (int)
    {
    }

    void run()
    {
        auto f (bind_handler (
            std::bind (&foo, std::placeholders::_1),
            42));
        f();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(bind_handler,core,beast);

} // beast
