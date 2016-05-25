//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/websocket/detail/stream_base.hpp>

#include <beast/unit_test/suite.hpp>
#include <initializer_list>
#include <climits>

namespace beast {
namespace websocket {
namespace detail {

class stream_base_test : public beast::unit_test::suite
{
public:
    void testClamp()
    {
        expect(detail::clamp(
            std::numeric_limits<std::uint64_t>::max()) ==
                std::numeric_limits<std::size_t>::max());
    }

    void run() override
    {
        testClamp();
    }
};

BEAST_DEFINE_TESTSUITE(stream_base,websocket,beast);

} // detail
} // websocket
} // beast

