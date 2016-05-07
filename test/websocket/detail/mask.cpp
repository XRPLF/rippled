//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/websocket/detail/mask.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {
namespace websocket {
namespace detail {

class mask_test : public beast::unit_test::suite
{
public:
    struct test_generator
    {
        using result_type = std::uint32_t;

        result_type n = 0;

        void
        seed(std::seed_seq const&)
        {
        }

        std::uint32_t
        operator()()
        {
            return n++;
        }
    };

    void run() override
    {
        maskgen_t<test_generator> mg;
        expect(mg() != 0);
    }
};

BEAST_DEFINE_TESTSUITE(mask,websocket,beast);

} // detail
} // websocket
} // beast

