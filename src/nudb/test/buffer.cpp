//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/detail/buffer.hpp>

#include <beast/unit_test/suite.hpp>
#include <type_traits>

namespace nudb {
namespace test {

class buffer_test : public beast::unit_test::suite
{
public:
    void
    run()
    {
        using buffer = nudb::detail::buffer;
        static_assert(std::is_default_constructible<buffer>::value, "");
#if 0
        static_assert(std::is_copy_constructible<buffer>::value, "");
        static_assert(std::is_copy_assignable<buffer>::value, "");
#else
        static_assert(! std::is_copy_constructible<buffer>::value, "");
        static_assert(! std::is_copy_assignable<buffer>::value, "");
#endif
        static_assert(std::is_move_constructible<buffer>::value, "");
        static_assert(std::is_move_assignable<buffer>::value, "");

        {
            buffer b;
        }
        {
            buffer b1(1024);
            BEAST_EXPECT(b1.size() == 1024);
            buffer b2(std::move(b1));
            BEAST_EXPECT(b1.size() == 0);
            BEAST_EXPECT(b2.size() == 1024);
        }
        {
            buffer b1(1024);
            BEAST_EXPECT(b1.size() == 1024);
            buffer b2;
            b2 = std::move(b1);
            BEAST_EXPECT(b1.size() == 0);
            BEAST_EXPECT(b2.size() == 1024);
        }

#if 0
        {
            buffer b1(1024);
            BEAST_EXPECT(b1.size() == 1024);
            buffer b2(b1);
            BEAST_EXPECT(b1.size() == 1024);
            BEAST_EXPECT(b2.size() == 1024);
        }
        {
            buffer b1(1024);
            BEAST_EXPECT(b1.size() == 1024);
            buffer b2;
            b2 = b1;
            BEAST_EXPECT(b1.size() == 1024);
            BEAST_EXPECT(b2.size() == 1024);
        }
#endif
    }
};

BEAST_DEFINE_TESTSUITE(buffer, test, nudb);

} // test
} // nudb
