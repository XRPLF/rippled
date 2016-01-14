//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/json/json_reader.h>
#include <ripple/test/jtx.h>
#include <beast/asio/streambuf.h>
#include <beast/http/parser.h>
#include <beast/unit_test/suite.h>
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <array>
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>
#include <type_traits>

#if 0//BOOST_VERSION >= 105800

#include <boost/endian/arithmetic.hpp>
template <class Int>
Int native_to_big(Int n);
{
    return boost::endian::native_to_big<Int>(n);
}

template <class Int>
Int big_to_native(Int b)
{
    return boost::endian::big_to_native<Int>(b);
}

#else

template <class Int>
Int native_to_big(Int n);

template <class Int>
Int big_to_native(Int b);

template<>
std::uint16_t
native_to_big<std::uint16_t>(std::uint16_t n)
{
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&n);
    std::swap(p[0], p[1]);
    return n;
}

template<>
std::uint64_t
native_to_big<std::uint64_t>(std::uint64_t n)
{
    return 0;
}

template<>
std::uint16_t
big_to_native<uint16_t>(std::uint16_t b)
{
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&b);
    std::swap(p[0], p[1]);
    return b;
}

template<>
std::uint64_t
big_to_native<uint64_t>(std::uint64_t b)
{
    return 0;
}

#endif

namespace ripple {
namespace test {

/*

async_write use cases:

We have a std::string and just want to send it
    - contiguous
    - size known ahead of time

We have ConstBufferSequence and just want to send it
    - size known ahead of time
    - discontiguous

Caller can produce data of unknown size


*/

namespace detail {

struct frame_header
{
    int op;
    bool fin;
    bool mask;
    bool rsv1;
    bool rsv2;
    bool rsv3;
    std::uint64_t len;
    std::array<std::uint8_t, 4> key;

    // Used internally to hold the header
    std::size_t bytes = 0;
    std::uint8_t front[2];
};

// Return # of additional bytes needed
// 0 == done, sb is consumed
template <class StreamBuf>
std::size_t
read_frame_header(frame_header& fh, StreamBuf& sb)
{
    using namespace boost::asio;
    std::array<std::uint8_t, 10> b;
    auto const n = buffer_copy(
        buffer(b), sb.data());
    std::size_t need = 2;
    if(n < need)
        return need - n;
    std::uint64_t len = b[1] & 0x7f;
    if(len == 126)
        need += 2;
    else if(len == 127)
        need += 8;
    auto const mask = b[1] & 0x80;
    if(mask)
        need += 4;
    if(n < need)
        return need - n;
    sb.consume(need);
    fh.op = b[0] & 0x0f;
    fh.fin = b[0] & 0x80;
    fh.rsv1 = b[0] & 0x40;
    fh.rsv2 = b[0] & 0x20;
    fh.rsv3 = b[0] & 0x10;
    fh.mask = mask;
    if(len < 126)
    {
        fh.len = len;
    }
    else if(len == 126)
    {
        std::uint16_t& d = *reinterpret_cast<
            std::uint16_t*>(&b[2]);
        fh.len = big_to_native<std::uint16_t>(d);
    }
    else
    {
        std::uint64_t& d = *reinterpret_cast<
            std::uint64_t*>(&b[2]);
        fh.len = big_to_native<std::uint64_t>(d);
    }
    if(mask)
    {
        //
    }
    return 0;
}

template<
    class StreamBuf,
    class ConstBuffers>
void
write_frame_payload(StreamBuf& sb, ConstBuffers const& cb)
{
    using namespace boost::asio;
    sb.commit(buffer_copy(
        sb.prepare(buffer_size(cb)), cb));
}

template<
    class StreamBuf,
    class ConstBuffers
>
void
write_frame(StreamBuf& sb, ConstBuffers const& cb)
{
    using namespace boost::asio;
    int  const op   = 1;
    bool const fin  = true;
    bool const mask = false;
    std::array<std::uint8_t, 10> b;
    b[0] = (fin ? 0x80 : 0x00) | op;
    b[1] = mask ? 0x80 : 0x00;
    auto const len = buffer_size(cb);
    if (len <= 125)
    {
        b[1] |= len;
        sb.commit(buffer_copy(
            sb.prepare(2), buffer(&b[0], 2)));
    }
    else if (len <= 65535)
    {
        b[1] = 126;
        std::uint16_t& d = *reinterpret_cast<
            std::uint16_t*>(&b[2]);
        d = native_to_big<std::uint16_t>(len);
        sb.commit(buffer_copy(
            sb.prepare(4), buffer(&b[0], 4)));
    }
    else
    {
        b[1] = 127;
        std::uint64_t& d = *reinterpret_cast<
            std::uint64_t*>(&b[2]);
        d = native_to_big<std::uint64_t>(len);
        sb.commit(buffer_copy(
            sb.prepare(10), buffer(&b[0], 10)));
    }
    if(mask)
    {
    }
    write_frame_payload(sb, cb);
}

} // detail

//------------------------------------------------------------------------------

template <
    class Stream,
    class Allocator = std::allocator<char>
>
class wsock
{
private:
    static_assert(! std::is_const<Stream>::value, "");

    using error_code = boost::system::error_code;

    Stream next_layer_;
    boost::asio::io_service::strand strand_;

public:
    using next_layer_type =
        std::remove_reference_t<Stream>;

    using lowest_layer_type =
        typename next_layer_type::lowest_layer_type;

    wsock(wsock&&) = default;
    wsock(wsock const&) = delete;
    wsock& operator= (wsock const&) = delete;
    wsock& operator= (wsock&&) = delete;

    template <class... Args>
    explicit
    wsock(Args&&... args)
        : next_layer_(std::forward<Args>(args)...)
        , strand_(next_layer_.get_io_service())
    {
    }

    boost::asio::io_service&
    get_io_service()
    {
        return next_layer_.lowest_layer().get_io_service();
    }

    next_layer_type&
    next_layer()
    {
        return next_layer_;
    }

    next_layer_type const&
    next_layer() const
    {
        return next_layer_;
    }

    lowest_layer_type&
    lowest_layer()
    {
        return next_layer_.lowest_layer();
    }

    lowest_layer_type const&
    lowest_layer() const
    {
        return next_layer_.lowest_layer();
    }

private:
    // Handler is a deduced context
    template <class Handler>
    struct write_op
        : std::enable_shared_from_this<write_op<Handler>>
    {
        std::decay_t<Handler> h_;
        beast::asio::basic_streambuf<Allocator> sb_;

        explicit
        write_op(Handler&& h)
            : h_(std::forward<Handler>(h))
        {
        }

        void
        operator()(error_code const& ec,
            std::size_t bytes_transferred)
        {
        }

        void
        on_write (error_code const& ec,
            std::size_t bytes_transferred)
        {
        }
    };
public:

    template <
        class ConstBuffers,
        class Handler>
    void
    async_write(ConstBuffers const& cb,
        Handler&& h)
    {
        auto op = std::make_shared<write_op<Handler>>(
            std::forward<Handler>(h));
        test::detail::write_frame(op->sb_, cb);
        boost::asio::async_write(next_layer_, op->sb_.data(),
            strand_.wrap(
                [op](error_code const& ec, std::size_t bytes_transferred)
                    { op->on_write(ec, bytes_transferred); }));
    }

    // write a text message
    template <class ConstBuffers>
    void
    write(ConstBuffers const& cb)
    {
        beast::asio::streambuf sb;
        test::detail::write_frame(sb, cb);
        boost::asio::write(next_layer_, sb.data());
    }

    // read a message
    template <class StreamBuf>
    void
    read(StreamBuf& sb)
    {
        beast::asio::streambuf b;
        for(;;)
        {
            test::detail::frame_header fh;
            for(;;)
            {
                auto const need =
                    test::detail::read_frame_header(fh, b);
                if(need == 0)
                    break;
                b.commit(boost::asio::read(
                    next_layer_, b.prepare(need)));
            }
            sb.commit(boost::asio::read(
                next_layer_, sb.prepare(fh.len)));
            if(fh.fin)
                break;
        }
    }
};

//------------------------------------------------------------------------------

class wsc_test : public beast::unit_test::suite
{
public:
    template <class ConstBufferSequence>
    std::string
    buffer_string (ConstBufferSequence const& b)
    {
        using namespace boost::asio;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    void run() override
    {
        using namespace jtx;
        auto cfg = std::make_unique<Config>();
        setupConfigForUnitTests(*cfg);
        Env env(*this,
            []
            {
                auto cfg = std::make_unique<Config>();
                setupConfigForUnitTests(*cfg);
                (*cfg)["server"].append("port_ws");
                (*cfg)["port_ws"].set("port", "6007");
                (*cfg)["port_ws"].set("ip", "127.0.0.1");
                (*cfg)["port_ws"].set("protocol", "ws");
                return cfg;
            }());

        using namespace boost::asio;
        io_service ios;
        boost::optional<io_service::work> work(ios);
        std::thread t([&]{ ios.run(); });
        {
            ip::tcp::socket sock(ios);
            wsock<ip::tcp::socket&> ws(sock);
            sock.connect({
                ip::address::from_string("127.0.0.1"), 6007});
            write(sock, buffer(
                "GET / HTTP/1.1\r\n"
                "Host: server.example.com\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Origin: http://example.com\r\n"
                "Sec-WebSocket-Protocol: chat, superchat\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n"
                ));
            streambuf sb;
            boost::asio::read_until(sock, sb, "\r\n\r\n");
            using namespace beast;
            http::body b;
            http::message m;
            http::parser p(m, b, false);
            auto const result = p.write(sb.data());
            if (result.first || ! p.complete())
                throw std::runtime_error(result.first.message());
            sb.consume(result.second);

            ws.write(buffer(
                R"( { "id" : 1, "command" : "server_info" }\n)"));
            ws.read(sb);
            Json::Reader jr;
            Json::Value jv;
            jr.parse(buffer_string(sb.data()), jv);
            sb.consume(sb.size());
            //log << pretty(jv);

            auto const gw = Account("gateway");
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const USD = gw["USD"];
            env.fund(XRP(10000), "alice", bob, gw);
            env.trust(USD(10000), "alice", bob);
            env(pay(gw, alice, USD(1000)));
            env(offer(alice, USD(10), XRP(1000)));

            Json::Value jp;
            jp["id"] = 2;
            jp["command"] = "ripple_path_find";
            jp["source_account"] = toBase58(bob.id());
            jp["source_currencies"][0u]["currency"] = "XRP";
            jp["destination_account"] = toBase58(bob.id());
            jp["destination_amount"] = USD(1).value().getJson(0);
            ws.write(buffer(to_string(jp)));
            ws.read(sb);
            jr.parse(buffer_string(sb.data()), jv);
            log << pretty(jv);
        }
        work = boost::none;
        t.join();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(wsc,test,ripple);

} // test
} // ripple
