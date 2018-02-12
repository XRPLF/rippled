//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/websocket/stream.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <beast/unit_test/suite.hpp>
#include <beast/test/pipe_stream.hpp>
#include <beast/test/yield_to.hpp>
#include <initializer_list>
#include <climits>

namespace beast {
namespace websocket {
namespace detail {

static
bool
operator==(frame_header const& lhs, frame_header const& rhs)
{
    return
        lhs.op == rhs.op &&
        lhs.fin == rhs.fin &&
        lhs.mask == rhs.mask &&
        lhs.rsv1 == rhs.rsv1 &&
        lhs.rsv2 == rhs.rsv2 &&
        lhs.rsv3 == rhs.rsv3 &&
        lhs.len == rhs.len &&
        lhs.key == rhs.key;
}

class frame_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    void testCloseCodes()
    {
        BEAST_EXPECT(! is_valid_close_code(0));
        BEAST_EXPECT(! is_valid_close_code(1));
        BEAST_EXPECT(! is_valid_close_code(999));
        BEAST_EXPECT(! is_valid_close_code(1004));
        BEAST_EXPECT(! is_valid_close_code(1005));
        BEAST_EXPECT(! is_valid_close_code(1006));
        BEAST_EXPECT(! is_valid_close_code(1016));
        BEAST_EXPECT(! is_valid_close_code(2000));
        BEAST_EXPECT(! is_valid_close_code(2999));
        BEAST_EXPECT(is_valid_close_code(1000));
        BEAST_EXPECT(is_valid_close_code(1002));
        BEAST_EXPECT(is_valid_close_code(3000));
        BEAST_EXPECT(is_valid_close_code(4000));
        BEAST_EXPECT(is_valid_close_code(5000));
    }

    struct test_fh : frame_header
    {
        test_fh()
        {
            op = detail::opcode::text;
            fin = false;
            mask = false;
            rsv1 = false;
            rsv2 = false;
            rsv3 = false;
            len = 0;
            key = 0;
        }
    };

    void testFrameHeader()
    {
        using stream_type =
            beast::websocket::stream<test::pipe::stream&>;
        test::pipe p{ios_};

        // good frame fields
        {
            stream_type::role_type role =
                stream_type::role_type::client;

            auto check =
                [&](frame_header const& fh)
                {
                    fh_streambuf b;
                    write(b, fh);
                    close_code code;
                    stream_type stream{p.server};
                    stream.open(role);
                    detail::frame_header fh1;
                    auto const n =
                        stream.read_fh1(fh1, b, code);
                    if(! BEAST_EXPECT(! code))
                        return;
                    if(! BEAST_EXPECT(b.size() == n))
                        return;
                    stream.read_fh2(fh1, b, code);
                    if(! BEAST_EXPECT(! code))
                        return;
                    if(! BEAST_EXPECT(b.size() == 0))
                        return;
                    BEAST_EXPECT(fh1 == fh);
                };

            test_fh fh;

            check(fh);

            role = stream_type::role_type::server;
            fh.mask = true;
            fh.key = 1;
            check(fh);

            fh.len = 1;
            check(fh);

            fh.len = 126;
            check(fh);

            fh.len = 65535;
            check(fh);

            fh.len = 65536;
            check(fh);

            fh.len = 65537;
            check(fh);
        }

        // bad frame fields
        {
            stream_type::role_type role = stream_type::role_type::client;

            auto check =
                [&](frame_header const& fh)
                {
                    fh_streambuf b;
                    write(b, fh);
                    close_code code;
                    stream_type stream{p.server};
                    stream.open(role);
                    frame_header fh1;
                    auto const n =
                        stream.read_fh1(fh1, b, code);
                    if(code)
                    {
                        pass();
                        return;
                    }
                    if(! BEAST_EXPECT(b.size() == n))
                        return;
                    stream.read_fh2(fh1, b, code);
                    if(! BEAST_EXPECT(code))
                        return;
                    if(! BEAST_EXPECT(b.size() == 0))
                        return;
                };

            test_fh fh;

            fh.op = detail::opcode::close;
            fh.fin = true;
            fh.len = 126;
            check(fh);
            fh.len = 0;

            fh.rsv1 = true;
            check(fh);
            fh.rsv1 = false;

            fh.rsv2 = true;
            check(fh);
            fh.rsv2 = false;

            fh.rsv3 = true;
            check(fh);
            fh.rsv3 = false;

            fh.op = detail::opcode::rsv3;
            check(fh);
            fh.op = detail::opcode::text;

            fh.op = detail::opcode::ping;
            fh.fin = false;
            check(fh);
            fh.fin = true;

            fh.mask = true;
            check(fh);

            role = stream_type::role_type::server;
            fh.mask = false;
            check(fh);
        }
    }

    void bad(std::initializer_list<std::uint8_t> bs)
    {
        using stream_type =
            beast::websocket::stream<test::pipe::stream&>;
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        test::pipe p{ios_};
        static stream_type::role_type constexpr role = stream_type::role_type::client;
        std::vector<std::uint8_t> v{bs};
        fh_streambuf b;
        b.commit(buffer_copy(b.prepare(v.size()), buffer(v)));
        stream_type stream{p.server};
        stream.open(role);
        close_code code;
        detail::frame_header fh;
        auto const n =
            stream.read_fh1(fh, b, code);
        if(code)
        {
            pass();
            return;
        }
        if(! BEAST_EXPECT(b.size() == n))
            return;
        stream.read_fh2(fh, b, code);
        if(! BEAST_EXPECT(code))
            return;
        if(! BEAST_EXPECT(b.size() == 0))
            return;
    }

    void testBadFrameHeaders()
    {
        // bad frame fields
        //
        // can't be created by the library
        // so we produce them manually.

        bad({0, 126, 0, 125});
        bad({0, 127, 0, 0, 0, 0, 0, 0, 255, 255});
    }

    void run() override
    {
        testCloseCodes();
        testFrameHeader();
        testBadFrameHeaders();
    }
};

BEAST_DEFINE_TESTSUITE(frame,websocket,beast);

} // detail
} // websocket
} // beast
