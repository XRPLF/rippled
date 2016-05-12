//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/message.hpp>

#include <beast/http/headers.hpp>
#include <beast/unit_test/suite.hpp>
#include <type_traits>

namespace beast {
namespace http {

class message_test : public beast::unit_test::suite
{
public:
    struct Arg1
    {
        bool moved = false;

        Arg1() = default;

        Arg1(Arg1&& other)
        {
            other.moved = true;
        }
    };

    struct Arg2 { };
    struct Arg3 { };

    // default constructible Body
    struct default_body
    {
        using value_type = std::string;
    };

    // 1-arg constructible Body
    struct one_arg_body
    {
        struct value_type
        {
            explicit
            value_type(Arg1 const&)
            {
            }

            explicit
            value_type(Arg1&& arg)
            {
                Arg1 arg_(std::move(arg));
            }
        };
    };

    // 2-arg constructible Body
    struct two_arg_body
    {
        struct value_type
        {
            value_type(Arg1 const&, Arg2 const&)
            {
            }
        };
    };

    void testConstruction()
    {
        static_assert(std::is_constructible<
            message<true, default_body, headers>>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, headers>, Arg1>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, headers>, Arg1 const>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, headers>, Arg1 const&>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, headers>, Arg1&&>::value, "");

        static_assert(! std::is_constructible<
            message<true, one_arg_body, headers>>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, headers>,
                Arg1, headers::allocator_type>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, headers>, std::piecewise_construct_t,
                std::tuple<Arg1>>::value, "");

        static_assert(std::is_constructible<
            message<true, two_arg_body, headers>, std::piecewise_construct_t,
                std::tuple<Arg1, Arg2>>::value, "");

        static_assert(std::is_constructible<
            message<true, two_arg_body, headers>, std::piecewise_construct_t,
                std::tuple<Arg1, Arg2>, std::tuple<headers::allocator_type>>::value, "");

        {
            Arg1 arg1;
            message<true, one_arg_body, headers>{std::move(arg1)};
            expect(arg1.moved);
        }

        {
            headers h;
            h.insert("User-Agent", "test");
            message<true, one_arg_body, headers> m{Arg1{}, h};
            expect(h["User-Agent"] == "test");
            expect(m.headers["User-Agent"] == "test");
        }
        {
            headers h;
            h.insert("User-Agent", "test");
            message<true, one_arg_body, headers> m{Arg1{}, std::move(h)};
            expect(! h.exists("User-Agent"));
            expect(m.headers["User-Agent"] == "test");
        }
    }

    void run() override
    {
        testConstruction();
    }
};

BEAST_DEFINE_TESTSUITE(message,http,beast);

} // http
} // beast

