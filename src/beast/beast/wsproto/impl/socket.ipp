//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_WSPROTO_IMPL_SOCKET_IPP_INCLUDED
#define BEAST_WSPROTO_IMPL_SOCKET_IPP_INCLUDED

#include <beast/wsproto/detail/hybi13.h>
#include <beast/wsproto/impl/accept_op.ipp>
#include <beast/wsproto/impl/close_op.ipp>
#include <beast/wsproto/impl/handshake_op.ipp>
#include <beast/wsproto/impl/header_op.ipp>
#include <beast/wsproto/impl/read_msg_op.ipp>
#include <beast/wsproto/impl/read_op.ipp>
#include <beast/wsproto/impl/write_msg_op.ipp>
#include <beast/wsproto/impl/write_op.ipp>
#include <beast/asio/append_buffers.h>
#include <beast/asio/static_streambuf.h>
#include <beast/asio/streambuf.h>
#include <boost/endian/buffers.hpp>

namespace beast {
namespace wsproto {

namespace detail {

template<class _>
error_code
socket_base::prepare_fh()
{
    error_code ec;
    
    if((ec = detail::validate_fh(role_, rd_fh_)))
        return ec;

    // continuation without an active message
    if(! rd_cont_ && rd_fh_.op == opcode::cont)
        return error::frame_header_invalid;

    // new data frame when continuation expected
    if(rd_cont_ && ! is_control(rd_fh_.op) &&
            rd_fh_.op != opcode::cont)
        return error::frame_header_invalid;

    if(rd_fh_.mask)
        prepare_key(rd_key_, rd_fh_.key);

    if(! is_control(rd_fh_.op))
    {
        if(rd_fh_.op != opcode::cont)
        {
            rd_op_ = rd_fh_.op;
            rd_need_ = rd_fh_.len;
        }
        rd_cont_ = ! rd_fh_.fin;
    }

    return ec;
}

template<class Streambuf>
void
socket_base::write_close(Streambuf& sb,
    close::value code, std::string reason)
{
    using namespace boost::asio;
    using namespace boost::endian;
    frame_header fh;
    fh.op = opcode::close;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = code == close::none ? 0 :
        2 + reason.size();
    if((fh.mask = (role_ == role_type::client)))
        fh.key = maskgen_();
    detail::write(sb, fh);
    if(code == close::none)
        return;
    detail::prepared_key_type key;
    if(fh.mask)
        detail::prepare_key(key, fh.key);
    {
        std::uint8_t b[2];
        ::new(&b[0]) big_uint16_buf_t{
            (std::uint16_t)code};
        auto d = sb.prepare(2);
        buffer_copy(d, buffer(b));
        if(fh.mask)
            detail::mask_inplace(d, key);
        sb.commit(2);
    }
    if(reason.empty())
        return;
    if(reason.size() > 123)
        reason.resize(123);
    // TODO utf8_check(reason);
    {
        auto d = sb.prepare(reason.size());
        buffer_copy(d, buffer(reason));
        if(fh.mask)
            detail::mask_inplace(d, key);
        sb.commit(reason.size());
    }
}

template<class Streambuf>
void
socket_base::write_ping(Streambuf& sb,
    opcode::value op, std::string data)
{
    using namespace boost::asio;
    frame_header fh;
    fh.op = op;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = data.size();
    if((fh.mask = (role_ == role_type::client)))
        fh.key = maskgen_();
    detail::write(sb, fh);
    if(data.empty())
        return;
    detail::prepared_key_type key;
    if(fh.mask)
        detail::prepare_key(key, fh.key);
    if(data.size() > 123)
        data.resize(123);
    // TODO utf8_check(data); VFALCO Is this necessary?
    auto d = sb.prepare(data.size());
    buffer_copy(d, buffer(data));
    if(fh.mask)
        detail::mask_inplace(d, key);
    sb.commit(data.size());
}

} // detail

template<class Stream>
template<class... Args>
socket<Stream>::socket(Args&&... args)
    : stream_(std::forward<Args>(args)...)
{
    decorate([](auto const&) { });
}

template<class Stream>
void
socket<Stream>::handshake(std::string const& host,
    std::string const& resource, error_code& ec)
{
    // VFALCO Used for tests, not production quality
    {
        auto m = make_upgrade(host, resource);
        beast::asio::streambuf sb;
        beast::http::write(sb, m);
        boost::asio::write(stream_, sb.data(), ec);
        if(ec)
            return;
    }
    {
        boost::asio::streambuf sb;
        boost::asio::read_until(stream_, sb, "\r\n\r\n", ec);
        if(ec)
            return;
        http::body b;
        http::message m;
        http::parser p(m, b, false);
        auto const result = p.write(sb.data());
        if (result.first || ! p.complete())
            throw std::runtime_error(result.first.message());
        sb.consume(result.second);
    }
    role_ = role_type::client;
}

template<class Stream>
template<class Handler>
void
socket<Stream>::async_handshake(std::string const& host,
    std::string const& resource, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    role_ = role_type::client;
    // TODO
    throw std::runtime_error("unimplemented");
}

template<class Stream>
template<class Buffers>
void
socket<Stream>::accept(Buffers const& b, error_code& ec)
{
    role_ = role_type::server;
    // TODO
    throw std::runtime_error("unimplemented");
}

template<class Stream>
void
socket<Stream>::accept(
    beast::http::message const& m, error_code& ec)
{
    role_ = role_type::server;
    // TODO
    throw std::runtime_error("unimplemented");
}

template<class Stream>
template<class Buffers, class Handler>
void
socket<Stream>::async_accept(Buffers&& buffers, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    accept_op<std::decay_t<Handler>>{*this,
        std::forward<Buffers>(buffers),
            std::forward<Handler>(h)}();
}

template<class Stream>
template<class Handler>
void
socket<Stream>::async_accept_request(
    beast::http::message const& m, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    accept_op<std::decay_t<Handler>>{*this,
        m, std::forward<Handler>(h), nullptr}();
}

template<class Stream>
void
socket<Stream>::close(std::uint16_t code,
    std::string const& description, error_code& ec)
{
}

template<class Stream>
template<class Handler>
void
socket<Stream>::async_close(std::uint16_t code,
    std::string const& description, Handler&& handler)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
}

template<class Stream>
void
socket<Stream>::read(frame_header& fh, error_code& ec)
{
    using namespace boost::asio;
    for(;;)
    {
        // read frame header
        {
            detail::fh_buffer buf;
            boost::asio::read(stream_,
                buffer(buf.data(), 2), ec);
            if(ec)
                return;
            boost::asio::read(stream_,
                mutable_buffers_1(buf.data() + 2,
                    detail::decode_fh1(rd_fh_, buf)), ec);
            if(ec)
                return;
            ec = detail::decode_fh2(rd_fh_, buf);
            if(ec)
                return;
        }
        ec = prepare_fh();
        if(ec)
            return;
        if(! is_control(rd_fh_.op))
        {
            fh = rd_fh_;
            break;
        }
        // read control frame payload
        std::array<std::uint8_t, 125> buf;
        mutable_buffers_1 mb(buf.data(), rd_fh_.len);
        if(rd_fh_.len > 0)
        {
            boost::asio::read(stream_, mb, ec);
            if(ec)
                return;
            if(rd_fh_.mask)
                detail::mask_inplace(mb, rd_key_);
        }
        //
        // TODO handle control frame
        //
    }
}

template<class Stream>
template<class Handler>
void
socket<Stream>::async_read(frame_header& fh, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    header_op<std::decay_t<Handler>>{
        *this, fh, std::forward<Handler>(h)}();
}

template<class Stream>
template<class Buffers>
std::size_t
socket<Stream>::read(
    Buffers const& bs, error_code& ec)
{
    if(rd_need_ == 0)
        throw std::logic_error("bad read state");
    auto const n = boost::asio::read(
        stream_, bs, detail::at_most{rd_need_}, ec);
    if(ec)
        return n;
    rd_need_ -= n;
    if(rd_fh_.mask)
        detail::mask_inplace(bs, rd_key_);
    return n;
}

template<class Stream>
template<class Buffers, class Handler>
void
socket<Stream>::async_read(
    Buffers&& b, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code, std::size_t)>::value,
            "Type does not meet the handler requirements");
    read_op<std::decay_t<Buffers>,
        std::decay_t<Handler>>{
            *this, std::forward<Buffers>(b),
                std::forward<Handler>(h)}();
}

template<class Stream>
template<class ConstBufferSequence>
void
socket<Stream>::write(bool fin,
    ConstBufferSequence const& bs, error_code& ec)
{
    frame_header fh;
    fh.op = wr_op_;
    fh.fin = fin;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = buffer_size(bs);
    if((fh.mask = (role_ == role_type::client)))
        fh.key = maskgen_();

    asio::static_streambuf_n<14> fh_buf;
    detail::write<asio::static_streambuf>(fh_buf, fh);
    if(fh.mask)
    {
        detail::prepared_key_type key;
        detail::prepare_key(key, fh.key);
        std::unique_ptr<std::uint8_t[]> p(
            new std::uint8_t[fh.len]);
        boost::asio::mutable_buffers_1 mb{
            p.get(), fh.len};
        boost::asio::buffer_copy(mb, bs);
        detail::mask_inplace(mb, key);
        boost::asio::write(stream_,
            beast::asio::append_buffers(
                fh_buf.data(), mb), ec);
    }
    else
    {
        boost::asio::write(stream_,
            beast::asio::append_buffers(
                fh_buf.data(), bs), ec);
    }
}

template<class Stream>
template<class Buffers, class Handler>
void
socket<Stream>::async_write(bool fin, Buffers&& bs, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    write_op<std::decay_t<Buffers>, std::decay_t<Handler>>{
        *this, fin, std::forward<Buffers>(bs),
            std::forward<Handler>(h)}();
}

//------------------------------------------------------------------------------

template<class Stream>
template<class Streambuf>
void
socket<Stream>::write_error(Streambuf& sb, error_code const& ec)
{
    beast::http::message m;
    std::string const body = ec.message();
    m.request(false);
    m.version(beast::http::http_1_1());
    m.status(400);
    m.reason("Bad request");
    // VFALCO Do we close on a failed upgrade request?
    //        Maybe set_option(keep_alive(true)) or something.
    if(keep_alive_)
        m.headers.append("Connection", "Keep-Alive");
    else
        m.headers.append("Connection", "Close");
    m.headers.append("Content-Type", "text/html");
    m.headers.append("Content-Length",
        std::to_string(body.size()));
    (*decorate_)(m);
    beast::http::write(sb, m);
}

template<class Stream>
template<class Streambuf>
void
socket<Stream>::write_response(Streambuf& sb,
    beast::http::message const& req)
{
    beast::http::message m;
    m.request(false);
    m.status(101);
    m.reason("Switching Protocols");
    m.version(beast::http::http_1_1());
    m.headers.append("Connection", "upgrade");
    m.headers.append("Upgrade", "websocket");
    auto const key =
        req.headers["Sec-WebSocket-Key"];
    m.headers.append("Sec-WebSocket-Key", key);
    m.headers.append("Sec-WebSocket-Accept",
        detail::make_sec_ws_accept(key));
    (*decorate_)(m);
    beast::http::write(sb, m);
}

template<class Stream>
beast::http::message
socket<Stream>::make_upgrade(std::string const& host,
    std::string const& resource)
{
    beast::http::message m;
    m.request(true);
    m.version(beast::http::http_1_1());
    m.method(beast::http::method_t::http_get);
    m.url(resource);
    m.headers.append("Connection", "upgrade");
    m.headers.append("Upgrade", "websocket");
    m.headers.append("Host", host);
    m.headers.append("Sec-WebSocket-Key",
        detail::make_sec_ws_key(maskgen_));
    m.headers.append("Sec-WebSocket-Version", "13");
    (*decorate_)(m);
    return m;
}

template<class Stream>
beast::asio::streambuf
socket<Stream>::make_response(
    beast::http::message const& r)
{
    beast::asio::streambuf sb;
    write_response(sb, r);
    return sb;
}

template<class Stream>
error_code
socket<Stream>::do_accept(beast::http::message const& r)
{
    auto err =
        [&](auto const& reason)
        {
            return error::bad_upgrade_request;
        };
    if(r.method() != beast::http::method_t::http_get)
        return err("Bad HTTP method");
    if(r.version() != beast::http::http_1_1())
        return err("Bad HTTP version");
    if(! r.headers.exists("Host"))
        return err("Missing Host field");
    //if(r.headers["Upgrade"] != "websocket")...
    return {};
}

//------------------------------------------------------------------------------

template<class Stream, class Streambuf>
void
read_msg(socket<Stream>& ws, Streambuf& sb, error_code& ec)
{
    for(;;)
    {
        frame_header fh;
        ws.read(fh, ec);
        if(ec)
            return;
        auto bs = sb.prepare(fh.len);
        ws.read(bs, ec);
        if(ec)
            return;
        sb.commit(fh.len);
        if(fh.fin)
            break;
    }
}

template<class Stream, class Streambuf, class Handler>
void
async_read_msg(socket<Stream>& ws, Streambuf& sb, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    detail::read_msg_op<Stream, Streambuf,
        std::decay_t<Handler>>{ws, sb,
            std::forward<Handler>(h)}();
}

template<class Stream, class Buffers>
void
write_msg(socket<Stream>& ws, Buffers const& bs, error_code& ec)
{
}

template<class Stream, class Buffers, class Handler>
void
async_write_msg(socket<Stream>& ws, Buffers&& bs, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
#if 0
    detail::write_msg_op<Stream, std::decay_t<Buffers>,
        std::decay_t<Handler>>{ws, std::forward<Buffers>(bs),
            std::forward<Handler>(h)}();
#else
    ws.async_write(true, std::forward<Buffers>(bs),
        std::forward<Handler>(h));
#endif
}

} // wsproto
} // beast

#endif
