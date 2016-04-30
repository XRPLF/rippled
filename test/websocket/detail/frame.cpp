//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/websocket/detail/frame.hpp>
#include <beast/unit_test/suite.hpp>
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

class frame_test : public beast::unit_test::suite
{
public:
    void testCloseCodes()
    {
        expect(! is_valid(0));
        expect(! is_valid(1));
        expect(! is_valid(999));
        expect(! is_valid(1004));
        expect(! is_valid(1005));
        expect(! is_valid(1006));
        expect(! is_valid(1016));
        expect(! is_valid(2000));
        expect(! is_valid(2999));
        expect(is_valid(1000));
        expect(is_valid(1002));
        expect(is_valid(3000));
        expect(is_valid(4000));
        expect(is_valid(5000));
    }

    struct test_fh : frame_header
    {
        test_fh()
        {
            op = opcode::text;
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
        // good frame headers
        {
            role_type role = role_type::client;

            auto check =
                [&](frame_header const& fh)
                {
                    fh_streambuf sb;
                    write(sb, fh);
                    frame_header fh1;
                    close_code::value code;
                    auto const n = read_fh1(
                        fh1, sb, role, code);
                    if(! expect(! code))
                        return;
                    if(! expect(sb.size() == n))
                        return;
                    read_fh2(fh1, sb, role, code);
                    if(! expect(! code))
                        return;
                    if(! expect(sb.size() == 0))
                        return;
                    expect(fh1 == fh);
                };

            test_fh fh;

            check(fh);

            role = role_type::server;
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

            fh.len = std::numeric_limits<std::uint64_t>::max();
            check(fh);
        }

        // bad frame headers
        {
            role_type role = role_type::client;

            auto check =
                [&](frame_header const& fh)
                {
                    fh_streambuf sb;
                    write(sb, fh);
                    frame_header fh1;
                    close_code::value code;
                    auto const n = read_fh1(
                        fh1, sb, role, code);
                    if(code)
                    {
                        pass();
                        return;
                    }
                    if(! expect(sb.size() == n))
                        return;
                    read_fh2(fh1, sb, role, code);
                    if(! expect(code))
                        return;
                    if(! expect(sb.size() == 0))
                        return;
                };

            test_fh fh;

            fh.op = opcode::close;
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

            fh.op = opcode::rsv3;
            check(fh);
            fh.op = opcode::text;

            fh.op = opcode::ping;
            fh.fin = false;
            check(fh);
            fh.fin = true;

            fh.mask = true;
            check(fh);

            role = role_type::server;
            fh.mask = false;
            check(fh);
        }
    }
    
    void bad(std::initializer_list<std::uint8_t> bs)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        static role_type constexpr role =
            role_type::client;
        std::vector<std::uint8_t> v{bs};
        fh_streambuf sb;
        sb.commit(buffer_copy(
            sb.prepare(v.size()), buffer(v)));
        frame_header fh;
        close_code::value code;
        auto const n = read_fh1(fh, sb, role, code);
        if(code)
        {
            pass();
            return;
        }
        if(! expect(sb.size() == n))
            return;
        read_fh2(fh, sb, role, code);
        if(! expect(code))
            return;
        if(! expect(sb.size() == 0))
            return;
    }

    void testBadFrameHeaders()
    {
        // bad frame headers
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
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(frame,websocket,beast);

} // detail
} // websocket
} // beast

