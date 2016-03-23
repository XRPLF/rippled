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
#include <beast/wsproto/impl/write_op.ipp>
#include <beast/asio/append_buffers.h>
#include <beast/asio/buffers_adapter.h>
#include <beast/asio/static_streambuf.h>
#include <beast/asio/streambuf.h>
#include <boost/endian/buffers.hpp>

#include <cstdio>

namespace beast {
namespace wsproto {

namespace detail {

template<class _>
void
socket_base::prepare_fh(close::value& code)
{
    // continuation without an active message
    if(! rd_cont_ && rd_fh_.op == opcode::cont)
    {
        code = close::protocol_error;
        return;
    }
    // new data frame when continuation expected
    if(rd_cont_ && ! is_control(rd_fh_.op) &&
            rd_fh_.op != opcode::cont)
    {
        code = close::protocol_error;
        return;
    }
    if(rd_fh_.mask)
        prepare_key(rd_key_, rd_fh_.key);
    if(! is_control(rd_fh_.op))
    {
        if(rd_fh_.op != opcode::cont)
            rd_op_ = rd_fh_.op;
        rd_need_ = rd_fh_.len;
        rd_cont_ = ! rd_fh_.fin;
    }
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
    if(data.size() > 125)
        data.resize(125);
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
    auto d = sb.prepare(data.size());
    buffer_copy(d, buffer(data));
    if(fh.mask)
        detail::mask_inplace(d, key);
    sb.commit(data.size());
}

} // detail

//------------------------------------------------------------------------------

template<class Stream>
template<class... Args>
socket<Stream>::socket(Args&&... args)
    : stream_(std::forward<Args>(args)...)
{
    decorate([](auto const&) { });
}

template<class Stream>
template<class Decorator>
void
socket<Stream>::decorate(Decorator&& d)
{
    static_assert(detail::is_handler<Decorator,
        void(beast::http::message&)>::value,
            "Decorator requirements not met");
    decorate_ = std::make_unique<
        detail::decorator<std::decay_t<Decorator>>>(
            std::forward<Decorator>(d));
}

template<class Stream>
void
socket<Stream>::accept()
{
    error_code ec;
    accept(boost::asio::null_buffers{}, ec);
    detail::maybe_throw(ec, "accept");
}

template<class Stream>
template<class Buffers>
void
socket<Stream>::accept(Buffers const& bs)
{
    error_code ec;
    accept(bs, ec);
    detail::maybe_throw(ec, "accept");
}

template<class Stream>
template<class Buffers>
void
socket<Stream>::accept(Buffers const& bs, error_code& ec)
{
    using namespace boost::asio;
    boost::asio::streambuf sb;
    sb.commit(buffer_copy(
        sb.prepare(buffer_size(bs)), bs));
    boost::asio::read_until(stream_, sb, "\r\n\r\n", ec);
    if(ec)
        return;
    std::string body;
    http::message m;
    http::parser p(
        [&](void const* data, std::size_t len)
        {
            auto begin =
                reinterpret_cast<char const*>(data);
            auto end = begin + len;
            body.append(begin, end);
        }, m, true);
    auto result = p.write(sb.data());
    if((ec = result.first))
        return;
    accept(m, ec);
}

template<class Stream>
void
socket<Stream>::accept(beast::http::message const& m)
{
    error_code ec;
    accept(m, ec);
    detail::maybe_throw(ec, "accept");
}

template<class Stream>
void
socket<Stream>::accept(
    beast::http::message const& m, error_code& ec)
{
    beast::asio::streambuf sb;
    auto req_ec = do_accept(m);
    if(req_ec)
        write_error(sb, req_ec);
    else
        write_response(sb, m);
    boost::asio::write(stream_, sb.data(), ec);
    if(ec)
        return;
    ec = req_ec;
    if(ec)
        return;
    role_ = role_type::server;
}

template<class Stream>
template<class Buffers, class AcceptHandler>
void
socket<Stream>::async_accept(
    Buffers const& bs, AcceptHandler&& h)
{
    static_assert(detail::is_handler<AcceptHandler,
        void(error_code)>::value,
            "AcceptHandler requirements not met");
    accept_op<std::decay_t<AcceptHandler>>{
        std::forward<AcceptHandler>(h), *this, bs}();
}

template<class Stream>
template<class AcceptHandler>
void
socket<Stream>::async_accept_request(
    beast::http::message const& m, AcceptHandler&& h)
{
    static_assert(detail::is_handler<AcceptHandler,
        void(error_code)>::value,
            "AcceptHandler requirements not met");
    accept_op<std::decay_t<AcceptHandler>>{
        std::forward<AcceptHandler>(h), *this, m, nullptr}();
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
template<class HandshakeHandler>
void
socket<Stream>::async_handshake(std::string const& host,
    std::string const& resource, HandshakeHandler&& h)
{
    static_assert(detail::is_handler<HandshakeHandler,
        void(error_code)>::value,
            "HandshakeHandler requirements not met");
role_ = role_type::client; // TODO Do this in op
    handshake_op<std::decay_t<HandshakeHandler>>{
        std::forward<HandshakeHandler>(h), *this}();
}

template<class Stream>
void
socket<Stream>::close(std::uint16_t code,
    std::string const& description, error_code& ec)
{
}

template<class Stream>
template<class CloseHandler>
void
socket<Stream>::async_close(CloseHandler&& h)
{
    static_assert(detail::is_handler<CloseHandler,
        void(error_code)>::value,
            "CloseHandler requirements not met");
    close_op<std::decay_t<CloseHandler>>{
        std::forward<CloseHandler>(h), *this};
}

template<class Stream>
template<class CloseHandler>
void
socket<Stream>::async_close(std::uint16_t code,
    std::string const& description, CloseHandler&& h)
{
    static_assert(detail::is_handler<CloseHandler,
        void(error_code)>::value,
            "CloseHandler requirements not met");
    close_op<std::decay_t<CloseHandler>>{
        std::forward<CloseHandler>(h), *this};
}

template<class Stream>
void
socket<Stream>::read(frame_header& fh, error_code& ec)
{
    using namespace boost::asio;
    assert(rd_need_ == 0);
    for(;;)
    {
        close::value code{};
        asio::static_streambuf_n<139> sb;
        // read header
        {
            sb.commit(boost::asio::read(
                stream_, sb.prepare(2), ec));
            if(ec)
                break;
            auto const n = detail::read_fh1(
                rd_fh_, sb, role_, code);
            if(! code)
            {
                sb.commit(boost::asio::read(
                    stream_, sb.prepare(n), ec));
                if(ec)
                    break;
                detail::read_fh2(
                    rd_fh_, sb, role_, code);
                if(! code)
                    prepare_fh(code);
            }
            if(code)
            {
                do_close(code, ec);
                break;
            }
        }
        if(! detail::is_control(rd_fh_.op))
        {
            fh = rd_fh_;
            break;
        }
        // read control payload
        if(rd_fh_.len > 0)
        {
            auto mb = sb.prepare(rd_fh_.len);
            sb.commit(boost::asio::read(stream_, mb, ec));
            if(ec)
                break;
            if(rd_fh_.mask)
                detail::mask_inplace(mb, rd_key_);
            sb.commit(rd_fh_.len);
        }
        // do control
        if(rd_fh_.op == opcode::ping ||
            rd_fh_.op == opcode::pong)
        {
            // VFALCO We should avoid a memory
            // alloc, use char[] here instead?
            std::string data;
            detail::read(data, sb.data(), code);
            if(code)
            {
                do_close(code, ec);
                break;
            }
            sb.reset();
            write_ping<asio::static_streambuf>(sb,
                rd_fh_.op == opcode::ping ?
                    opcode::pong : opcode::ping, data);
            boost::asio::write(stream_, sb.data(), ec);
            if(ec)
                break;
            continue;
        }
        assert(rd_fh_.op == opcode::close);
        {
            if(closing_)
            {
                ec = error::closed;
                break;
            }
            // VFALCO We should not use std::string
            //        in reason_code here
            reason_code rc;
            detail::read(rc, sb.data(), code);
            if(code)
            {
                rc.code = code;
                rc.reason = "";
            }
            else if(! rc.code)
            {
                rc.code = close::normal;
                rc.reason = "";
            }
            else if(! detail::is_valid(*rc.code))
            {
                rc.code = close::protocol_error;
                rc.reason = "";
            }
            sb.reset();
            write_close<asio::static_streambuf>(
                sb, *rc.code, rc.reason);
            closing_ = true;
            boost::asio::write(stream_, sb.data(), ec);
            if(ec)
                break;
            ec = error::closed;
            break;
        }
    }
}

template<class Stream>
template<class ReadHandler>
void
socket<Stream>::async_read(frame_header& fh, ReadHandler&& h)
{
    static_assert(detail::is_handler<ReadHandler,
        void(error_code)>::value,
            "ReadHandler requirements not met");
    header_op<std::decay_t<ReadHandler>>{
        std::forward<ReadHandler>(h), *this, fh}();
}

template<class Stream>
template<class Buffers>
std::size_t
socket<Stream>::read(Buffers const& bs, error_code& ec)
{
    using namespace boost::asio;
    assert(rd_need_ != 0);
    std::size_t tot = 0;
    beast::asio::buffers_adapter<Buffers> ba(bs);
    while(! ec && ba.max_size() > 0 && rd_need_ > 0)
    {
        auto const mb = ba.prepare(
            std::min(rd_need_, ba.max_size()));
        auto const n = stream_.read_some(mb, ec);
        if(ec)
            break;
        rd_need_ -= n;
        if(rd_fh_.mask)
            detail::mask_inplace(
                beast::asio::prepare_buffers(
                    n, mb), rd_key_);
        ba.commit(n);
        if(rd_op_ == opcode::text)
        {
            if(! rd_utf8_check_.write(ba.data()) ||
                (rd_need_ == 0 && rd_fh_.fin &&
                    ! rd_utf8_check_.finish()))
            {
                do_close(close::bad_payload, ec);
                return 0;
            }
        }
        ba.consume(n);
        tot += n;
    }
    return tot;
}

template<class Stream>
template<class Buffers, class ReadPayloadHandler>
void
socket<Stream>::async_read(
    Buffers const& bs, ReadPayloadHandler&& h)
{
    static_assert(detail::is_handler<ReadPayloadHandler,
        void(error_code, std::size_t)>::value,
            "ReadPayloadHandler requirements not met");
    read_op<Buffers, std::decay_t<ReadPayloadHandler>>{
        std::forward<ReadPayloadHandler>(h), *this, bs}();
}

template<class Stream>
template<class Buffers>
void
socket<Stream>::write(opcode::value op, bool fin,
    Buffers const& bs, error_code& ec)
{
    using namespace boost::asio;
    if(wr_cont_ && op != opcode::cont)
        throw std::invalid_argument("cont opcode expected");
    else if(! wr_cont_ && op == opcode::cont)
        throw std::invalid_argument("non-cont opcode expected");
    wr_cont_ = ! fin;
    frame_header fh;
    fh.op = op;
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

template<class... Args>
using handler_type =
    typename boost::asio::handler_type<Args...>::type;

template<class Stream>
template<class Buffers, class WriteHandler>
BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler,
    void(boost::system::error_code))
socket<Stream>::async_write(opcode::value op, bool fin,
    Buffers const& bs, WriteHandler&& h)
{
    using namespace boost::asio;
    static_assert(detail::is_handler<WriteHandler,
        void(error_code)>::value,
            "WriteHandler requirements not met");
    assert((! wr_cont_ && op != opcode::cont) ||
        (wr_cont_ && op == opcode::cont));
    wr_cont_ = ! fin;
    boost::asio::detail::async_result_init<WriteHandler,
        void(boost::system::error_code)> init(
            std::forward<WriteHandler>(h));
    write_op<Buffers, handler_type<WriteHandler,
        void(error_code)>>{std::forward<WriteHandler>(
            init.handler), *this, op, fin, bs}();
    return init.result.get();
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

template<class Stream>
void
socket<Stream>::do_close(
    close::value code, error_code& ec)
{
    closing_ = true;
    asio::static_streambuf_n<139> sb;
    write_close<asio::static_streambuf>(sb, code);
    boost::asio::write(stream_, sb.data(), ec);
    if(! ec)
        ec = error::closed;
}

//------------------------------------------------------------------------------

template<class Stream, class Streambuf>
void
read_msg(socket<Stream>& ws, opcode::value& op,
    Streambuf& sb, error_code& ec)
{
    bool cont = false;
    for(;;)
    {
        frame_header fh;
        ws.read(fh, ec);
        if(ec)
            return;
        if(fh.len > 0)
        {
            ws.read(sb.prepare(fh.len), ec);
            if(ec)
                return;
        }
        sb.commit(fh.len);
        if(! cont)
        {
            op = fh.op;
            cont = true;
        }
        if(fh.fin)
            break;
    }
}

template<class Stream, class Streambuf, class ReadHandler>
void
async_read_msg(socket<Stream>& ws, opcode::value& op,
    Streambuf& sb, ReadHandler&& h)
{
    static_assert(detail::is_handler<ReadHandler,
        void(error_code)>::value,
            "ReadHandler requirements not met");
    detail::read_msg_op<Stream, Streambuf,
        std::decay_t<ReadHandler>>{ws, op, sb,
            std::forward<ReadHandler>(h)}();
}

template<class Stream, class Buffers>
void
write_msg(socket<Stream>& ws, opcode::value op,
    Buffers const& bs, error_code& ec)
{
    ws.write(op, true, bs, ec);
}

template<class Stream, class Buffers, class Handler>
void
async_write_msg(socket<Stream>& ws,
    opcode::value op, Buffers const& bs, Handler&& h)
{
    static_assert(detail::is_handler<Handler,
        void(error_code)>::value,
            "WriteHandler requirements not met");
    ws.async_write(op, true, bs, std::forward<Handler>(h));
}

} // wsproto
} // beast

#endif
