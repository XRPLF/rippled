//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_WSPROTO_BASIC_SOCKET_H_INCLUDED
#define RIPPLE_WSPROTO_BASIC_SOCKET_H_INCLUDED

#include <BeastConfig.h>
#include <beast/http/parser.h>
#include <beast/is_call_possible.h>
#include <boost/asio.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/optional.hpp>
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>

//------------------------------------------------------------------------------

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
inline
std::uint16_t
native_to_big<std::uint16_t>(std::uint16_t n)
{
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&n);
    std::swap(p[0], p[1]);
    return n;
}

template<>
inline
std::uint64_t
native_to_big<std::uint64_t>(std::uint64_t n)
{
    return 0;
}

template<>
inline
std::uint16_t
big_to_native<uint16_t>(std::uint16_t b)
{
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&b);
    std::swap(p[0], p[1]);
    return b;
}

template<>
inline
std::uint64_t
big_to_native<uint64_t>(std::uint64_t b)
{
    return 0;
}

#endif

//------------------------------------------------------------------------------

namespace wsproto {

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

    // next offset in key, [0-4)
    int offset = 0;
};

namespace detail {

// decode first 2 bytes of frame header
// return: number of additional bytes needed
template<class = void>
std::size_t
decode_fh1(frame_header& fh, std::uint8_t const* p)
{
    std::size_t need;
    fh.len = p[1] & 0x7f;
    switch(fh.len)
    {
        case 126: need = 2; break;
        case 127: need = 8; break;
        default:
            need = 0;
    }
    if((fh.mask = (p[1] & 0x80)))
        need += 4;
    fh.op   = p[0] & 0x0f;
    fh.fin  = p[0] & 0x80;
    fh.rsv1 = p[0] & 0x40;
    fh.rsv2 = p[0] & 0x20;
    fh.rsv3 = p[0] & 0x10;
    fh.offset = 0;
    return need;
}

// decode remainder of frame header
template<class = void>
void
decode_fh2(frame_header& fh, std::uint8_t const* p)
{
    switch(fh.len)
    {
    case 126:
        fh.len =
            (std::uint64_t(p[0])<<8) + p[1];
        p += 2;
        break;
    case 127:
        fh.len = 0;
        for(int i = 0; i < 8; ++i)
            fh.len = (fh.len << 8) + p[i];
        p += 8;
        break;
    default:
        break;
    }
    if(fh.mask)
        std::memcpy(fh.key.data(), p, 4);
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
        b[1] |= 126;
        std::uint16_t& d = *reinterpret_cast<
            std::uint16_t*>(&b[2]);
        d = native_to_big<std::uint16_t>(len);
        sb.commit(buffer_copy(
            sb.prepare(4), buffer(&b[0], 4)));
    }
    else
    {
        b[1] |= 127;
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

//------------------------------------------------------------------------------

// establish client connection
template<class Stream, class Handler>
class connect_op
{
    using error_code = boost::system::error_code;

    struct data
    {
        Stream& s;
        Handler h;
        
        data(Stream& s_, Handler&& h_)
            : s(s_)
            , h(std::forward<Handler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    connect_op(Stream& s_, Handler&& h_)
        : d_(std::make_shared<data>(
            s_, std::forward<Handler>(h_)))
    {
    }
};

// read a frame header
template<class Stream, class Handler>
class read_fh_op
{
    using error_code = boost::system::error_code;

    struct data
    {
        Stream& s;
        frame_header& fh;
        Handler h;
        int state = 0;
        std::array<std::uint8_t, 12> buf;

        data(Stream& s_, frame_header& fh_,
                Handler&& h_)
            : s(s_)
            , fh(fh_)
            , h(std::forward<Handler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_fh_op(read_fh_op&&) = default;
    read_fh_op(read_fh_op const&) = default;

    read_fh_op(Stream& s, frame_header& fh,
            Handler&& h)
        : d_(std::make_shared<data>(s, fh,
            std::forward<Handler>(h)))
    {
        using namespace boost::asio;
        async_read(d_->s, mutable_buffers_1(
            d_->buf.data(), 2), std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        if(ec == error::operation_aborted)
            return;
        if(! ec)
        {
            if(d_->state == 0)
            {
                d_->state = 1;
                async_read(d_->s, mutable_buffers_1(
                    d_->buf.data(), detail::decode_fh1(
                        d_->fh, d_->buf.data())),
                            std::move(*this));
                return;
            }
            detail::decode_fh2(
                d_->fh, d_->buf.data());
        }
        return d_->s.get_io_service().wrap(
            std::move(d_->h))(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, read_fh_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size,
            read_fh_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(
        read_fh_op* op)
    {
        return (op->d_->state != 0) ? true :
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(
        Function&& f, read_fh_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

// read a frame body
template<class Stream,
    class MutableBuffers, class Handler>
struct read_op
{
    using error_code = boost::system::error_code;

    struct data
    {
        Stream& s;
        frame_header fh;
        MutableBuffers b;
        Handler h;

        data(Stream& s_, frame_header const& fh_,
                MutableBuffers const& b_, Handler&& h_)
            : s(s_)
            , fh(fh_)
            , b(b_)
            , h(std::forward<Handler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    read_op(Stream& s, frame_header const& fh,
            MutableBuffers const& b, Handler&& h)
        : d_(std::make_shared<data>(s, fh, b,
            std::forward<Handler>(h)))
    {
        using namespace boost::asio;
        async_read(d_->s, d_->b,
            std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        if(ec == error::operation_aborted)
            return;
        if(d_->fh.mask)
        {
            // apply mask key
            // adjust d_->fh.offset
        }
        return d_->s.get_io_service().wrap(
            std::move(d_->h))(ec, std::move(d_->fh),
                bytes_transferred);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
                allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size,
            read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(
        read_op* op)
    {
        return boost_asio_handler_cont_helpers::
            is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(
        Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // detail

//------------------------------------------------------------------------------

template <
    class Stream,
    class Allocator = std::allocator<char>
>
class basic_socket
{
private:
    static_assert(! std::is_const<Stream>::value, "");

    using error_code = boost::system::error_code;

    Stream s_;
    boost::asio::io_service::strand st_;

public:
    using next_layer_type =
        std::remove_reference_t<Stream>;

    using lowest_layer_type =
        typename next_layer_type::lowest_layer_type;

    basic_socket(basic_socket&&) = default;
    basic_socket(basic_socket const&) = delete;
    basic_socket& operator= (basic_socket const&) = delete;
    basic_socket& operator= (basic_socket&&) = delete;

    template <class... Args>
    explicit
    basic_socket(Args&&... args)
        : s_(std::forward<Args>(args)...)
        , st_(s_.get_io_service())
    {
    }

    ~basic_socket()
    {
    }

    boost::asio::io_service&
    get_io_service()
    {
        return s_.lowest_layer().get_io_service();
    }

    next_layer_type&
    next_layer()
    {
        return s_;
    }

    next_layer_type const&
    next_layer() const
    {
        return s_;
    }

    lowest_layer_type&
    lowest_layer()
    {
        return s_.lowest_layer();
    }

    lowest_layer_type const&
    lowest_layer() const
    {
        return s_.lowest_layer();
    }

public:
    /** Request a WebSocket upgrade.
        Requirements:
            Stream is connected.
    */
    void
    connect(error_code& ec)
    {
        using namespace boost::asio;
        boost::asio::write(s_, buffer(
            "GET / HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"), ec);
        if(ec)
            return;
        streambuf sb;
        read_until(s_, sb, "\r\n\r\n");
        using namespace beast;
        http::body b;
        http::message m;
        http::parser p(m, b, false);
        auto const result = p.write(sb.data());
        if (result.first || ! p.complete())
            throw std::runtime_error(result.first.message());
        sb.consume(result.second);
    }

    // write a text message
    template <class ConstBuffers>
    void
    write(ConstBuffers const& cb)
    {
        boost::asio::streambuf sb;
        wsproto::detail::write_frame(sb, cb);
        boost::asio::write(s_, sb.data());
    }

public:
    // read a frame header asynchronously
    template<class Handler>
    void
    async_read_fh(frame_header& fh, Handler&& h)
    {
        static_assert(beast::is_call_possible<Handler,
            void(error_code)>::value,
                "Type does not meet the handler requirements");
        detail::read_fh_op<Stream, Handler>{
            s_, fh, std::forward<Handler>(h)};
    }

    // read a frame body asynchronously
    // requires buffer_size(b) == fh.len
    template<class MutableBuffers, class Handler>
    void
    async_read(frame_header const& fh,
        MutableBuffers const& b, Handler&& h)
    {
        static_assert(beast::is_call_possible<Handler,
            void(error_code)>::value,
                "Type does not meet the handler requirements");
        detail::read_op<Stream, MutableBuffers, Handler>{
            s_, fh, b, std::forward<Handler>(h)};
    }
};

} // wsproto

#endif
