//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/reason.hpp>

#include <beast/unit_test/suite.hpp>

namespace beast {
namespace http {

class reason_test : public unit_test::suite
{
public:
    void run() override
    {
        for(int i = 1; i <= 999; ++i)
            BEAST_EXPECT(reason_string(i) != nullptr);
    }
};

BEAST_DEFINE_TESTSUITE(reason,http,beast);

} // http
} // beast
