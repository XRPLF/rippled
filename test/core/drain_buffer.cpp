//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/drain_buffer.hpp>

#include <beast/core/type_traits.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {

static_assert(is_dynamic_buffer<drain_buffer>::value,
    "DynamicBuffer requirements not met");

class drain_buffer_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        using boost::asio::buffer_size;
        drain_buffer b;
        BEAST_EXPECT(buffer_size(b.prepare(0)) == 0);
        BEAST_EXPECT(buffer_size(b.prepare(100)) == 100);
        try
        {
            b.prepare(b.max_size() + 1);
            fail("", __FILE__, __LINE__);
        }
        catch(std::length_error const&)
        {
            pass();
        }
        b.prepare(10);
        BEAST_EXPECT(b.size() == 0);
        b.commit(10);
        BEAST_EXPECT(b.size() == 0);
        b.consume(10);
        BEAST_EXPECT(b.size() == 0);
        b.consume(1000);
        BEAST_EXPECT(b.size() == 0);
    }
};

BEAST_DEFINE_TESTSUITE(drain_buffer,core,beast);

} // beast
