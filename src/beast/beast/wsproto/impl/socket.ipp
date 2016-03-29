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

#include <beast/wsproto/teardown.h>
#include <beast/wsproto/detail/hybi13.h>
#include <beast/wsproto/impl/accept_op.ipp>
#include <beast/wsproto/impl/close_op.ipp>
#include <beast/wsproto/impl/handshake_op.ipp>
#include <beast/wsproto/impl/read_op.ipp>
#include <beast/wsproto/impl/read_some_op.ipp>
#include <beast/wsproto/impl/write_op.ipp>
#include <beast/asio/append_buffers.h>
#include <beast/asio/buffers_readstream.h>
#include <beast/asio/read_until.h>
#include <beast/asio/static_streambuf.h>
#include <beast/asio/streambuf.h>
#include <beast/asio/type_check.h>
#include <boost/endian/buffers.hpp>
#include <memory>
#include <utility>

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
socket_base::write_close(
    Streambuf& sb, close_reason const& cr)
{
    using namespace boost::asio;
    using namespace boost::endian;
    frame_header fh;
    fh.op = opcode::close;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = cr.code == close::none ?
        0 : 2 + cr.reason.size();
    if((fh.mask = (role_ == role_type::client)))
        fh.key = maskgen_();
    detail::write(sb, fh);
    if(cr.code != close::none)
    {
        detail::prepared_key_type key;
        if(fh.mask)
            detail::prepare_key(key, fh.key);
        {
            std::uint8_t b[2];
            ::new(&b[0]) big_uint16_buf_t{
                (std::uint16_t)cr.code};
            auto d = sb.prepare(2);
            buffer_copy(d, buffer(b));
            if(fh.mask)
                detail::mask_inplace(d, key);
            sb.commit(2);
        }
        if(! cr.reason.empty())
        {
            auto d = sb.prepare(cr.reason.size());
            buffer_copy(d, const_buffer(
                cr.reason.data(), cr.reason.size()));
            if(fh.mask)
                detail::mask_inplace(d, key);
            sb.commit(cr.reason.size());
        }
    }
}

template<class Streambuf>
void
socket_base::write_ping(Streambuf& sb,
    opcode::value op, ping_payload_type const& data)
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
    auto d = sb.prepare(data.size());
    buffer_copy(d,
        const_buffers_1(data.data(), data.size()));
    if(fh.mask)
        detail::mask_inplace(d, key);
    sb.commit(data.size());
}

} // detail

//------------------------------------------------------------------------------

template<class Stream>
template<class... Args>
socket<Stream>::socket(Args&&... args)
    : next_layer_(std::forward<Args>(args)...)
    , stream_(next_layer_)
{
    static_assert(asio::is_Stream<next_layer_type>::value,
        "Stream requirements not met");
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
void
socket<Stream>::accept(error_code& ec)
{
    accept(boost::asio::null_buffers{}, ec);
}

template<class Stream>
template<class ConstBufferSequence>
void
socket<Stream>::accept(
    ConstBufferSequence const& buffers)
{
    static_assert(beast::asio::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    accept(buffers, ec);
    detail::maybe_throw(ec, "accept");
}

template<class Stream>
template<class ConstBufferSequence>
void
socket<Stream>::accept(
    ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(beast::asio::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    stream_.buffer().commit(
        boost::asio::buffer_copy(
            stream_.buffer().prepare(
                boost::asio::buffer_size(buffers)),
                    buffers));
    boost::asio::read_until(next_layer_,
        stream_.buffer(), "\r\n\r\n", ec);
    // VFALCO What if ec == eof?
    if(ec)
        return;
    beast::http::body body;
    beast::http::message m;
    beast::http::parser p(m, body, true);
    auto const result = p.write(
        stream_.buffer().data());
    if(! p.complete())
    {
        ec = error::request_malformed;
        return;
    }
    if(result.first)
    {
        ec = error::request_malformed;
        return;
    }
    if(! ec)
    {
        stream_.buffer().consume(result.second);
        accept(m, ec);
    }
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
template<class AcceptHandler>
void
socket<Stream>::async_accept(AcceptHandler&& handler)
{
    static_assert(beast::asio::is_Handler<
        AcceptHandler, void(error_code)>::value,
            "AcceptHandler requirements not met");
    async_accept(boost::asio::null_buffers{},
        std::forward<AcceptHandler>(handler));
}

template<class Stream>
template<class ConstBufferSequence, class AcceptHandler>
void
socket<Stream>::async_accept(
    ConstBufferSequence const& bs, AcceptHandler&& h)
{
    static_assert(beast::asio::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    static_assert(beast::asio::is_Handler<
        AcceptHandler, void(error_code)>::value,
            "AcceptHandler requirements not met");
    accept_op<std::decay_t<AcceptHandler>>{
        std::forward<AcceptHandler>(h), *this, bs};
}

template<class Stream>
template<class AcceptHandler>
void
socket<Stream>::async_accept_request(
    beast::http::message const& m, AcceptHandler&& h)
{
    static_assert(beast::asio::is_Handler<
        AcceptHandler, void(error_code)>::value,
            "AcceptHandler requirements not met");
    accept_op<std::decay_t<AcceptHandler>>{
        std::forward<AcceptHandler>(h), *this, m, nullptr};
}

template<class Stream>
void
socket<Stream>::handshake(std::string const& host,
    std::string const& resource, error_code& ec)
{
    // VFALCO This could use improvement
    {
        auto m = make_upgrade(host, resource);
        beast::asio::streambuf sb;
        beast::http::write(sb, m);
        boost::asio::write(stream_, sb.data(), ec);
        if(ec)
            return;
    }
    {
        beast::asio::streambuf sb;
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
    std::string const& resource, HandshakeHandler&& handler)
{
    static_assert(beast::asio::is_Handler<
        HandshakeHandler, void(error_code)>::value,
            "HandshakeHandler requirements not met");
    handshake_op<std::decay_t<HandshakeHandler>>{
        std::forward<HandshakeHandler>(handler),
            *this, host, resource};
}

template<class Stream>
void
socket<Stream>::close(std::uint16_t code,
    std::string const& description, error_code& ec)
{
    // VFALCO TODO
}

template<class Stream>
template<class CloseHandler>
void
socket<Stream>::async_close(CloseHandler&& h)
{
    static_assert(asio::is_Handler<CloseHandler,
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
    static_assert(asio::is_Handler<CloseHandler,
        void(error_code)>::value,
            "CloseHandler requirements not met");
    close_op<std::decay_t<CloseHandler>>{
        std::forward<CloseHandler>(h), *this};
}

template<class Stream>
template<class Streambuf>
void
socket<Stream>::read_some(msg_info& mi,
    Streambuf& streambuf, error_code& ec)
{
    using namespace boost::asio;
    close::value code{};
    if(rd_need_ > 0)
        goto read_payload;
    for(;;)
    {
        // read header
        {
            asio::static_streambuf_n<139> fb;
            {
                fb.commit(boost::asio::read(
                    stream_, fb.prepare(2), ec));
                if(ec)
                    break;
                auto const n = detail::read_fh1(
                    rd_fh_, fb, role_, code);
                if(! code)
                {
                    fb.commit(boost::asio::read(
                        stream_, fb.prepare(n), ec));
                    if(ec)
                        break;
                    detail::read_fh2(
                        rd_fh_, fb, role_, code);
                    if(! code)
                        prepare_fh(code);
                }
                if(code)
                {
                    do_fail(code, ec);
                    break;
                }
            }
            if(detail::is_control(rd_fh_.op))
            {
                // read control payload
                if(rd_fh_.len > 0)
                {
                    auto mb = fb.prepare(rd_fh_.len);
                    fb.commit(boost::asio::read(stream_, mb, ec));
                    if(ec)
                        break;
                    if(rd_fh_.mask)
                        detail::mask_inplace(mb, rd_key_);
                    fb.commit(rd_fh_.len);
                }
                // do control
                if(rd_fh_.op == opcode::ping)
                {
                    ping_payload_type data;
                    detail::read(data, fb.data(), code);
                    if(code)
                    {
                        do_fail(code, ec);
                        break;
                    }
                    fb.reset();
                    write_ping<asio::static_streambuf>(
                        fb, opcode::pong, data);
                    boost::asio::write(stream_, fb.data(), ec);
                    if(ec)
                        break;
                    continue;
                }
                else if(rd_fh_.op == opcode::pong)
                {
                    ping_payload_type data;
                    detail::read(data, fb.data(), code);
                    if(code)
                    {
                        do_fail(code, ec);
                        break;
                    }
                    // VFALCO How to notify callers using
                    //        the synchronous interface?
                    continue;
                }
                assert(rd_fh_.op == opcode::close);
                {
                    // VFALCO Can this ever happen?
                    if(fail_)
                    {
                        ec = error::closed;
                        break;
                    }
                    // VFALCO We should not use std::string
                    //        in close_reason here
                    close_reason cr;
                    detail::read(cr, fb.data(), code);
                    if(code)
                    {
                        cr.code = code;
                        cr.reason = "";
                    }
                    else if(cr.code == close::none)
                    {
                        cr.code = close::normal;
                        cr.reason = "";
                    }
                    else if(! detail::is_valid(cr.code))
                    {
                        cr.code = close::protocol_error;
                        cr.reason = "";
                    }
                    fb.reset();
                    write_close<
                        asio::static_streambuf>(fb, cr);
                    // VFALCO Should we set fail_ here?
                    fail_ = true;
                    boost::asio::write(stream_, fb.data(), ec);
                    if(ec)
                        break;
                    ec = error::closed;
                    break;
                }
            }
            if(rd_fh_.len == 0 && ! rd_fh_.fin)
            {
                // empty frame
                continue;
            }
        }
        if(rd_need_ > 0)
        {
        read_payload:
            // read payload
            auto smb = streambuf.prepare(rd_need_);
            auto const bytes_transferred =
                stream_.read_some(smb, ec);
            if(ec)
                break;
            rd_need_ -= bytes_transferred;
            auto const pb = asio::prepare_buffers(
                bytes_transferred, smb);
            if(rd_fh_.mask)
                detail::mask_inplace(pb, rd_key_);
            if(rd_op_ == opcode::text)
            {
                if(! rd_utf8_check_.write(pb) ||
                    (rd_need_ == 0 && rd_fh_.fin &&
                        ! rd_utf8_check_.finish()))
                {
                    // invalid utf8
                    do_fail(close::bad_payload, ec);
                    break;
                }
            }
            streambuf.commit(bytes_transferred);
        }
        mi.op = rd_op_;
        mi.fin = rd_fh_.fin && rd_need_ == 0;
        break;
    }
    if(ec == error::closed)
    {
        wsproto_helpers::call_teardown(next_layer_, ec);
        if(! ec)
            ec = error::closed;
    }
}

template<class Stream>
template<class Streambuf, class ReadHandler>
void
socket<Stream>::async_read_some(msg_info& mi,
    Streambuf& streambuf, ReadHandler&& handler)
{
    using namespace boost::asio;
    static_assert(asio::is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    static_assert(asio::is_Handler<ReadHandler,
        void(error_code)>::value,
            "ReadHandler requirements not met");
    read_some_op<Streambuf, std::decay_t<ReadHandler>>{
        std::forward<ReadHandler>(handler),
            *this, mi, streambuf};
}

template<class Stream>
template<class ConstBufferSequence>
void
socket<Stream>::write(opcode::value op, bool fin,
    ConstBufferSequence const& bs, error_code& ec)
{
    static_assert(asio::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using namespace boost::asio;
    if(wr_cont_ && op != opcode::cont)
        throw std::invalid_argument("cont opcode expected");
    else if(! wr_cont_ && op == opcode::cont)
        throw std::invalid_argument("non-cont opcode expected");
    wr_cont_ = ! fin;
    detail::frame_header fh;
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

template<class Stream>
template<class ConstBufferSequence, class WriteHandler>
void
socket<Stream>::async_write(opcode::value op, bool fin,
    ConstBufferSequence const& bs, WriteHandler&& handler)
{
    static_assert(asio::is_Handler<WriteHandler,
        void(error_code)>::value,
            "WriteHandler requirements not met");
    static_assert(asio::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using namespace boost::asio;
    assert((! wr_cont_ && op != opcode::cont) ||
        (wr_cont_ && op == opcode::cont));
    wr_cont_ = ! fin;
    write_op<ConstBufferSequence, std::decay_t<
        WriteHandler>>{std::forward<WriteHandler>(
            handler), *this, op, fin, bs};
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
    decorate_(m);
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
    decorate_(m);
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
    decorate_(m);
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
    // VFALCO TODO More robust checking
    auto err =
        [&](auto const& reason)
        {
            return error::request_invalid;
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
socket<Stream>::do_response(
    beast::http::message const& m, error_code& ec)
{
    if(m.status() != 101)
    {
        ec = error::response_failed;
        return;
    }
    // VFALCO TODO Check Sec-WebSocket-Key and Sec-WebSocket-Accept
    //             Check version, subprotocols, etc
}

template<class Stream>
void
socket<Stream>::do_fail(
    close::value code, error_code& ec)
{
    // Fail the connection (per rfc6455)
    fail_ = true;
    asio::static_streambuf_n<139> sb;
    write_close<asio::static_streambuf>(sb, code);
    boost::asio::write(stream_, sb.data(), ec);
    if(! ec)
        ec = error::closed;
}

//------------------------------------------------------------------------------

template<class Stream, class Streambuf>
void
read(socket<Stream>& ws, opcode::value& op,
    Streambuf& streambuf, error_code& ec)
{
    msg_info mi;
    for(;;)
    {
        ws.read_some(mi, streambuf, ec);
        if(ec)
            break;
        op = mi.op;
        if(mi.fin)
            break;
    }
}

template<class Stream, class Streambuf, class ReadHandler>
BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler,
    void(error_code))
async_read(socket<Stream>& ws, opcode::value& op,
    Streambuf& streambuf, ReadHandler&& handler)
{
    static_assert(asio::is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    static_assert(asio::is_Handler<ReadHandler,
        void(error_code)>::value,
            "ReadHandler requirements not met");
    detail::read_op<Stream, Streambuf,
        std::decay_t<ReadHandler>>{
            std::forward<ReadHandler>(handler),
                ws, op, streambuf};
}

template<class Stream, class Buffers>
void
write_msg(socket<Stream>& ws, opcode::value op,
    Buffers const& bs, error_code& ec)
{
    static_assert(
        asio::is_ConstBufferSequence<Buffers>::value,
            "ConstBufferSequence requirements not met");
    ws.write(op, true, bs, ec);
}

template<class Stream, class Buffers, class Handler>
void
async_write_msg(socket<Stream>& ws,
    opcode::value op, Buffers const& bs, Handler&& h)
{
    static_assert(asio::is_Handler<Handler,
        void(error_code)>::value,
            "WriteHandler requirements not met");
    static_assert(
        asio::is_ConstBufferSequence<Buffers>::value,
            "ConstBufferSequence requirements not met");
    ws.async_write(op, true, bs, std::forward<Handler>(h));
}

} // wsproto
} // beast

#endif
