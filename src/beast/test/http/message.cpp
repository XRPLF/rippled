//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/message.hpp>

#include <beast/http/empty_body.hpp>
#include <beast/http/fields.hpp>
#include <beast/http/string_body.hpp>
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

    void testMessage()
    {
        static_assert(std::is_constructible<
            message<true, default_body, fields>>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, fields>, Arg1>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, fields>, Arg1 const>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, fields>, Arg1 const&>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, fields>, Arg1&&>::value, "");

        static_assert(! std::is_constructible<
            message<true, one_arg_body, fields>>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, fields>,
                Arg1, fields::allocator_type>::value, "");

        static_assert(std::is_constructible<
            message<true, one_arg_body, fields>, std::piecewise_construct_t,
                std::tuple<Arg1>>::value, "");

        static_assert(std::is_constructible<
            message<true, two_arg_body, fields>, std::piecewise_construct_t,
                std::tuple<Arg1, Arg2>>::value, "");

        static_assert(std::is_constructible<
            message<true, two_arg_body, fields>, std::piecewise_construct_t,
                std::tuple<Arg1, Arg2>, std::tuple<fields::allocator_type>>::value, "");

        {
            Arg1 arg1;
            message<true, one_arg_body, fields>{std::move(arg1)};
            BEAST_EXPECT(arg1.moved);
        }

        {
            fields h;
            h.insert("User-Agent", "test");
            message<true, one_arg_body, fields> m{Arg1{}, h};
            BEAST_EXPECT(h["User-Agent"] == "test");
            BEAST_EXPECT(m.fields["User-Agent"] == "test");
        }
        {
            fields h;
            h.insert("User-Agent", "test");
            message<true, one_arg_body, fields> m{Arg1{}, std::move(h)};
            BEAST_EXPECT(! h.exists("User-Agent"));
            BEAST_EXPECT(m.fields["User-Agent"] == "test");
        }

        // swap
        message<true, string_body, fields> m1;
        message<true, string_body, fields> m2;
        m1.url = "u";
        m1.body = "1";
        m1.fields.insert("h", "v");
        m2.method = "G";
        m2.body = "2";
        swap(m1, m2);
        BEAST_EXPECT(m1.method == "G");
        BEAST_EXPECT(m2.method.empty());
        BEAST_EXPECT(m1.url.empty());
        BEAST_EXPECT(m2.url == "u");
        BEAST_EXPECT(m1.body == "2");
        BEAST_EXPECT(m2.body == "1");
        BEAST_EXPECT(! m1.fields.exists("h"));
        BEAST_EXPECT(m2.fields.exists("h"));
    }

    struct MoveHeaders
    {
        bool moved_to = false;
        bool moved_from = false;

        MoveHeaders() = default;

        MoveHeaders(MoveHeaders&& other)
            : moved_to(true)
        {
            other.moved_from = true;
        }

        MoveHeaders& operator=(MoveHeaders&& other)
        {
            return *this;
        }
    };

    void testHeaders()
    {
        {
            using req_type = request_header;
            static_assert(std::is_copy_constructible<req_type>::value, "");
            static_assert(std::is_move_constructible<req_type>::value, "");
            static_assert(std::is_copy_assignable<req_type>::value, "");
            static_assert(std::is_move_assignable<req_type>::value, "");

            using res_type = response_header;
            static_assert(std::is_copy_constructible<res_type>::value, "");
            static_assert(std::is_move_constructible<res_type>::value, "");
            static_assert(std::is_copy_assignable<res_type>::value, "");
            static_assert(std::is_move_assignable<res_type>::value, "");
        }

        {
            MoveHeaders h;
            header<true, MoveHeaders> r{std::move(h)};
            BEAST_EXPECT(h.moved_from);
            BEAST_EXPECT(r.fields.moved_to);
            request<string_body, MoveHeaders> m{std::move(r)};
            BEAST_EXPECT(r.fields.moved_from);
            BEAST_EXPECT(m.fields.moved_to);
        }
    }

    void testFreeFunctions()
    {
        {
            request<empty_body> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.fields.insert("Upgrade", "test");
            BEAST_EXPECT(! is_upgrade(m));

            prepare(m, connection::upgrade);
            BEAST_EXPECT(is_upgrade(m));
            BEAST_EXPECT(m.fields["Connection"] == "upgrade");

            m.version = 10;
            BEAST_EXPECT(! is_upgrade(m));
        }
    }

    void testPrepare()
    {
        request<empty_body> m;
        m.version = 10;
        BEAST_EXPECT(! is_upgrade(m));
        m.fields.insert("Transfer-Encoding", "chunked");
        try
        {
            prepare(m);
            fail();
        }
        catch(std::exception const&)
        {
        }
        m.fields.erase("Transfer-Encoding");
        m.fields.insert("Content-Length", "0");
        try
        {
            prepare(m);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
        m.fields.erase("Content-Length");
        m.fields.insert("Connection", "keep-alive");
        try
        {
            prepare(m);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
        m.version = 11;
        m.fields.erase("Connection");
        m.fields.insert("Connection", "close");
        BEAST_EXPECT(! is_keep_alive(m));
    }

    void testSwap()
    {
        message<false, string_body, fields> m1;
        message<false, string_body, fields> m2;
        m1.status = 200;
        m1.version = 10;
        m1.body = "1";
        m1.fields.insert("h", "v");
        m2.status = 404;
        m2.reason = "OK";
        m2.body = "2";
        m2.version = 11;
        swap(m1, m2);
        BEAST_EXPECT(m1.status == 404);
        BEAST_EXPECT(m2.status == 200);
        BEAST_EXPECT(m1.reason == "OK");
        BEAST_EXPECT(m2.reason.empty());
        BEAST_EXPECT(m1.version == 11);
        BEAST_EXPECT(m2.version == 10);
        BEAST_EXPECT(m1.body == "2");
        BEAST_EXPECT(m2.body == "1");
        BEAST_EXPECT(! m1.fields.exists("h"));
        BEAST_EXPECT(m2.fields.exists("h"));
    }

    void run() override
    {
        testMessage();
        testHeaders();
        testFreeFunctions();
        testPrepare();
        testSwap();
    }
};

BEAST_DEFINE_TESTSUITE(message,http,beast);

} // http
} // beast
