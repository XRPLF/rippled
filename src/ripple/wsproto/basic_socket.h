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
#include <beast/asio/streambuf.h>
#include <beast/http/parser.h>
#include <beast/is_call_possible.h>
#include <boost/asio.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/optional.hpp>
#include <boost/version.hpp>
#if defined(BOOST_VERSION) && BOOST_VERSION >= 105800
#include <boost/endian/conversion.hpp>
#endif
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>

//------------------------------------------------------------------------------

#ifndef BOOST_ENDIAN_CONVERSION_HPP

namespace boost {
namespace endian {

template<class EndianReversible>
EndianReversible native_to_big(EndianReversible n) noexcept;

template<class EndianReversible>
EndianReversible big_to_native(EndianReversible b) noexcept;

template<>
inline
std::uint16_t
native_to_big<std::uint16_t>(std::uint16_t n) noexcept
{
    std::uint16_t b;
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&b);
    p[0] = (n & 0xff00) >> 8;
    p[1] =  n & 0x00ff;
    return b;
}

template<>
inline
std::uint64_t
native_to_big<std::uint64_t>(std::uint64_t n) noexcept
{
    std::uint64_t b;
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&b);
    p[0] = (n & 0xff00000000000000) >> 56;
    p[1] = (n & 0x00ff000000000000) >> 48;
    p[2] = (n & 0x0000ff0000000000) >> 40;
    p[3] = (n & 0x000000ff00000000) >> 32;
    p[4] = (n & 0x00000000ff000000) >> 24;
    p[5] = (n & 0x0000000000ff0000) >> 16;
    p[6] = (n & 0x000000000000ff00) >>  8;
    p[7] = (n & 0x00000000000000ff);
    return b;
}

template<>
inline
std::uint16_t
big_to_native<std::uint16_t>(std::uint16_t b) noexcept
{
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&b);
    return
        (std::uint16_t(p[0]) << 8) +
         std::uint16_t(p[1]);
}

template<>
inline
std::uint64_t
big_to_native<std::uint64_t>(std::uint64_t b) noexcept
{
    std::uint8_t* p =
        reinterpret_cast<std::uint8_t*>(&b);
    return
        (std::uint64_t(p[0]) << 56) +
        (std::uint64_t(p[1]) << 48) +
        (std::uint64_t(p[2]) << 40) +
        (std::uint64_t(p[3]) << 32) +
        (std::uint64_t(p[4]) << 24) +
        (std::uint64_t(p[5]) << 16) +
        (std::uint64_t(p[6]) <<  8) +
         std::uint64_t(p[7]);
}

} // endian
} // boost

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
    using namespace boost::endian;
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

//------------------------------------------------------------------------------

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
        void* p, std::size_t size, read_fh_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_fh_op* op)
    {
        return (op->d_->state != 0) ? true :
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_fh_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

//------------------------------------------------------------------------------

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

        template<class DeducedBuffers, class DeducedHandler>
        data(Stream& s_, frame_header const& fh_,
                DeducedBuffers&& b_, DeducedHandler&& h_)
            : s(s_)
            , fh(fh_)
            , b(std::forward<DeducedBuffers>(b_))
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class... Args>
    read_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
        using namespace boost::asio;
        async_read(d_->s, d_->b,
            std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        if(! ec)
        {
            if(d_->fh.mask)
            {
                // apply mask key
                // adjust d_->fh.offset
            }
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
        void* p, std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_op* op)
    {
        return boost_asio_handler_cont_helpers::
            is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

//------------------------------------------------------------------------------

// write a frame
template<class Stream, class Handler>
struct write_op
{
    using error_code = boost::system::error_code;

    struct data
    {
        Stream& s;
        frame_header fh;
        Handler h;
        beast::asio::streambuf sb;

        template<class ConstBuffers, class DeducedHandler>
        data(Stream& s_, frame_header const& fh_,
                ConstBuffers const& b, DeducedHandler&& h_)
            : s(s_)
            , fh(fh_)
            , h(std::forward<DeducedHandler>(h_))
        {
            write_frame(sb, b);
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class...Args>
    write_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
        using namespace boost::asio;
        async_write(d_->s, d_->sb.data(),
            std::move(*this));
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        if(! ec)
        {
        }
        return d_->s.get_io_service().wrap(
            std::move(d_->h))(ec, std::move(d_->fh));
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(write_op* op)
    {
        return boost_asio_handler_cont_helpers::
            is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, write_op* op)
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
        detail::read_fh_op<Stream,
            std::remove_cv_t<Handler>>{
                s_, fh, std::forward<Handler>(h)};
    }

    // read a frame body asynchronously
    // requires buffer_size(b) == fh.len
    template<class MutableBuffers, class Handler>
    void
    async_read(frame_header const& fh,
        MutableBuffers&& b, Handler&& h)
    {
        static_assert(beast::is_call_possible<Handler,
            void(error_code)>::value,
                "Type does not meet the handler requirements");
        detail::read_op<Stream,
            std::remove_cv_t<MutableBuffers>,
                std::remove_cv_t<Handler>>{s_, fh,
                    std::forward<MutableBuffers>(b),
                        std::forward<Handler>(h)};
    }

    // write a frame asynchronously
    template<class ConstBuffers, class Handler>
    void
    async_write(bool fin, ConstBuffers const& b, Handler&& h)
    {
        static_assert(beast::is_call_possible<Handler,
            void(error_code)>::value,
                "Type does not meet the handler requirements");
        frame_header fh;
        fh.op = 1;
        fh.fin = fin;
        fh.mask = false;
        fh.rsv1 = 0;
        fh.rsv2 = 0;
        fh.rsv3 = 0;
        fh.len = boost::asio::buffer_size(b);
        //fh.key =
        detail::write_op<Stream,
            std::remove_cv_t<Handler>>{s_, fh, b,
                std::forward<Handler>(h)};
    }
};

} // wsproto

#endif
