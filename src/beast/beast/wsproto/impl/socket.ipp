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
#include <beast/asio/async_completion.h>
#include <beast/asio/prepare_buffers.h>
#include <beast/asio/read_until.h>
#include <beast/asio/static_streambuf.h>
#include <beast/asio/streambuf.h>
#include <beast/asio/type_check.h>
#include <boost/endian/buffers.hpp>
#include <cassert>
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
            rd_opcode_ = rd_fh_.op;
        rd_need_ = rd_fh_.len;
        rd_cont_ = ! rd_fh_.fin;
    }
}

template<class Streambuf>
void
socket_base::write_close(
    Streambuf& sb, close_reason const& cr)
{
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
            boost::asio::buffer_copy(d,
                boost::asio::buffer(b));
            if(fh.mask)
                detail::mask_inplace(d, key);
            sb.commit(2);
        }
        if(! cr.reason.empty())
        {
            auto d = sb.prepare(cr.reason.size());
            boost::asio::buffer_copy(d,
                boost::asio::const_buffer(
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
    boost::asio::buffer_copy(d,
        boost::asio::const_buffers_1(
            data.data(), data.size()));
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
    static_assert(is_Stream<next_layer_type>::value,
        "Stream requirements not met");
}

template<class Stream>
void
socket<Stream>::accept(error_code& ec)
{
    accept(boost::asio::null_buffers{}, ec);
}

template<class Stream>
template<class AcceptHandler>
auto
socket<Stream>::async_accept(AcceptHandler&& handler)
{
    return async_accept(boost::asio::null_buffers{},
        std::forward<AcceptHandler>(handler));
}

template<class Stream>
template<class ConstBufferSequence>
void
socket<Stream>::accept(
    ConstBufferSequence const& buffers)
{
    static_assert(is_ConstBufferSequence<
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
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    stream_.buffer().commit(
        boost::asio::buffer_copy(
            stream_.buffer().prepare(
                boost::asio::buffer_size(buffers)),
                    buffers));
    auto const bytes_used =
        boost::asio::read_until(next_layer_,
            stream_.buffer(), "\r\n\r\n", ec);
    if(ec)
        return;
    beast::deprecated_http::body body;
    beast::deprecated_http::message m;
    beast::deprecated_http::parser p(m, body, true);
    auto const used = p.write(
        prepare_buffers(bytes_used,
            stream_.buffer().data()), ec);
    assert(p.complete());
    if(ec)
        return; // ec is a parser error code
    stream_.buffer().consume(used);
    accept(m, ec);
}

template<class Stream>
template<class ConstBufferSequence, class AcceptHandler>
auto
socket<Stream>::async_accept(
    ConstBufferSequence const& bs, AcceptHandler&& handler)
{
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    beast::async_completion<
        AcceptHandler, void(error_code)
            > completion(handler);
    accept_op<decltype(completion.handler)>{
        completion.handler, *this, bs};
    return completion.result.get();
}

template<class Stream>
void
socket<Stream>::accept(beast::deprecated_http::message const& m)
{
    error_code ec;
    accept(m, ec);
    detail::maybe_throw(ec, "accept");
}

template<class Stream>
void
socket<Stream>::accept(
    beast::deprecated_http::message const& m, error_code& ec)
{
    streambuf sb;
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
    // VFALCO TODO Respect keep alive setting, perform
    //             teardown if Connection: close.
}

template<class Stream>
template<class AcceptHandler>
auto
socket<Stream>::async_accept(
    beast::deprecated_http::message const& m, AcceptHandler&& handler)
{
    beast::async_completion<
        AcceptHandler, void(error_code)
            > completion(handler);
    accept_op<decltype(completion.handler)>{
        completion.handler, *this, m, nullptr};
    return completion.result.get();
}

template<class Stream>
void
socket<Stream>::handshake(std::string const& host,
    std::string const& resource, error_code& ec)
{
    // VFALCO This could use improvement
    {
        auto m = make_upgrade(host, resource);
        streambuf sb;
        beast::deprecated_http::write(sb, m);
        boost::asio::write(stream_, sb.data(), ec);
        if(ec)
            return;
    }
    {
        streambuf sb;
        boost::asio::read_until(stream_, sb, "\r\n\r\n", ec);
        if(ec)
            return;
        deprecated_http::body b;
        deprecated_http::message m;
        deprecated_http::parser p(m, b, false);
        // VFALCO This is certainly wrong.. throw?!
        auto const used = p.write(sb.data(), ec);
        // VFALCO Should we write EOF to indicate the end of the stream?
        if (ec || ! p.complete())
            throw std::runtime_error(ec.message());
        sb.consume(used);
    }
    role_ = role_type::client;
}

template<class Stream>
template<class HandshakeHandler>
auto
socket<Stream>::async_handshake(std::string const& host,
    std::string const& resource, HandshakeHandler&& handler)
{
    beast::async_completion<
        HandshakeHandler, void(error_code)
            > completion(handler);
    handshake_op<decltype(completion.handler)>{
        completion.handler, *this, host, resource};
    return completion.result.get();
}

template<class Stream>
void
socket<Stream>::close(
    close_reason const& cr, error_code& ec)
{
    assert(! wr_close_);
    wr_close_ = true;
    detail::frame_streambuf fb;
    write_close<static_streambuf>(fb, cr);
    boost::asio::write(stream_, fb.data(), ec);
    error_ = ec;
}

template<class Stream>
template<class CloseHandler>
auto
socket<Stream>::async_close(
    close_reason const& cr, CloseHandler&& handler)
{
    beast::async_completion<
        CloseHandler, void(error_code)
            > completion(handler);
    close_op<decltype(completion.handler)>{
        completion.handler, *this, cr};
    return completion.result.get();
}

template<class Stream>
template<class Streambuf>
void
socket<Stream>::read_some(msg_info& mi,
    Streambuf& streambuf, error_code& ec)
{
    close::value code{};
    for(;;)
    {
        if(rd_need_ == 0)
        {
            // read header
            detail::frame_streambuf fb;
            do_read_fh(fb, code, ec);
            if((error_ = ec))
                return;
            if(code)
                break;
            if(detail::is_control(rd_fh_.op))
            {
                // read control payload
                if(rd_fh_.len > 0)
                {
                    auto const mb =
                        fb.prepare(rd_fh_.len);
                    fb.commit(boost::asio::read(stream_, mb, ec));
                    if((error_ = ec))
                        return;
                    if(rd_fh_.mask)
                        detail::mask_inplace(mb, rd_key_);
                    fb.commit(rd_fh_.len);
                }
                if(rd_fh_.op == opcode::ping)
                {
                    ping_payload_type data;
                    detail::read(data, fb.data(), code);
                    if(code)
                        break;
                    fb.reset();
                    write_ping<static_streambuf>(
                        fb, opcode::pong, data);
                    boost::asio::write(stream_, fb.data(), ec);
                    if((error_ = ec))
                        return;
                    continue;
                }
                else if(rd_fh_.op == opcode::pong)
                {
                    ping_payload_type data;
                    detail::read(data, fb.data(), code);
                    if((error_ = ec))
                        break;
                    // VFALCO How to notify callers using
                    //        the synchronous interface?
                    continue;
                }
                assert(rd_fh_.op == opcode::close);
                {
                    detail::read(cr_, fb.data(), code);
                    if(code)
                        break;
                    if(! wr_close_)
                    {
                        auto cr = cr_;
                        if(cr.code == close::none)
                            cr.code = close::normal;
                        cr.reason = "";
                        fb.reset();
                        wr_close_ = true;
                        write_close<static_streambuf>(fb, cr);
                        boost::asio::write(stream_, fb.data(), ec);
                        if((error_ = ec))
                            return;
                    }
                    break;
                }
            }
            if(rd_need_ == 0 && ! rd_fh_.fin)
            {
                // empty frame
                continue;
            }
        }
        // read payload
        auto smb = streambuf.prepare(rd_need_);
        auto const bytes_transferred =
            stream_.read_some(smb, ec);
        if((error_ = ec))
            return;
        rd_need_ -= bytes_transferred;
        auto const pb = prepare_buffers(
            bytes_transferred, smb);
        if(rd_fh_.mask)
            detail::mask_inplace(pb, rd_key_);
        if(rd_opcode_ == opcode::text)
        {
            if(! rd_utf8_check_.write(pb) ||
                (rd_need_ == 0 && rd_fh_.fin &&
                    ! rd_utf8_check_.finish()))
            {
                code = close::bad_payload;
                break;
            }
        }
        streambuf.commit(bytes_transferred);
        mi.op = rd_opcode_;
        mi.fin = rd_fh_.fin && rd_need_ == 0;
        return;
    }
    if(code)
    {
        // Fail the connection (per rfc6455)
        if(! wr_close_)
        {
            wr_close_ = true;
            detail::frame_streambuf fb;
            write_close<static_streambuf>(fb, code);
            boost::asio::write(stream_, fb.data(), ec);
            if((error_ = ec))
                return;
        }
        wsproto_helpers::call_teardown(next_layer_, ec);
        if((error_ = ec))
            return;
        ec = error::failed;
        error_ = true;
        return;
    }
    if(! ec)
        wsproto_helpers::call_teardown(next_layer_, ec);
    if(! ec)
        ec = error::closed;
    error_ = ec;
}

template<class Stream>
template<class Streambuf, class ReadHandler>
auto
socket<Stream>::async_read_some(msg_info& mi,
    Streambuf& streambuf, ReadHandler&& handler)
{
    static_assert(beast::is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    beast::async_completion<
        ReadHandler, void(error_code)> completion(handler);
    read_some_op<Streambuf, decltype(completion.handler)>{
        completion.handler, *this, mi, streambuf};
    return completion.result.get();
}

template<class Stream>
template<class ConstBufferSequence>
void
socket<Stream>::write(opcode::value op, bool fin,
    ConstBufferSequence const& bs, error_code& ec)
{
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
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
    fh.len = boost::asio::buffer_size(bs);
    if((fh.mask = (role_ == role_type::client)))
        fh.key = maskgen_();
    detail::fh_streambuf fh_buf;
    detail::write<static_streambuf>(fh_buf, fh);
    if(fh.mask)
    {
        detail::prepared_key_type key;
        detail::prepare_key(key, fh.key);
        // VFALCO Could cap the buffer size if we
        //        use write_some
        std::unique_ptr<std::uint8_t[]> p(
            new std::uint8_t[fh.len]);
        boost::asio::mutable_buffers_1 mb{
            p.get(), fh.len};
        boost::asio::buffer_copy(mb, bs);
        detail::mask_inplace(mb, key);
        boost::asio::write(stream_,
            append_buffers(fh_buf.data(), mb), ec);
        error_ = ec;
    }
    else
    {
        boost::asio::write(stream_,
            append_buffers(fh_buf.data(), bs), ec);
        error_ = ec;
    }
}

template<class Stream>
template<class ConstBufferSequence, class WriteHandler>
auto
socket<Stream>::async_write(opcode::value op, bool fin,
    ConstBufferSequence const& bs, WriteHandler&& handler)
{
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    beast::async_completion<
        WriteHandler, void(error_code)
            > completion(handler);
    assert((! wr_cont_ && op != opcode::cont) ||
        (wr_cont_ && op == opcode::cont));
    wr_cont_ = ! fin;
    write_op<ConstBufferSequence, decltype(
        completion.handler)>{completion.handler,
            *this, op, fin, bs};
    return completion.result.get();
}

//------------------------------------------------------------------------------

template<class Stream>
template<class Streambuf>
void
socket<Stream>::write_error(Streambuf& sb, error_code const& ec)
{
    beast::deprecated_http::message m;
    std::string const body = ec.message();
    m.request(false);
    m.version(beast::deprecated_http::http_1_1());
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
    beast::deprecated_http::write(sb, m);
}

template<class Stream>
template<class Streambuf>
void
socket<Stream>::write_response(Streambuf& sb,
    beast::deprecated_http::message const& req)
{
    beast::deprecated_http::message m;
    m.request(false);
    m.status(101);
    m.reason("Switching Protocols");
    m.version(beast::deprecated_http::http_1_1());
    m.headers.append("Connection", "upgrade");
    m.headers.append("Upgrade", "websocket");
    auto const key =
        req.headers["Sec-WebSocket-Key"];
    m.headers.append("Sec-WebSocket-Key", key);
    m.headers.append("Sec-WebSocket-Accept",
        detail::make_sec_ws_accept(key));
    decorate_(m);
    beast::deprecated_http::write(sb, m);
}

template<class Stream>
beast::deprecated_http::message
socket<Stream>::make_upgrade(std::string const& host,
    std::string const& resource)
{
    beast::deprecated_http::message m;
    m.request(true);
    m.version(beast::deprecated_http::http_1_1());
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
error_code
socket<Stream>::do_accept(beast::deprecated_http::message const& r)
{
    // VFALCO TODO More robust checking
    auto err =
        [&](auto const& reason)
        {
            return error::request_invalid;
        };
    if(r.method() != beast::http::method_t::http_get)
        return err("Bad HTTP method");
    if(r.version() != beast::deprecated_http::http_1_1())
        return err("Bad HTTP version");
    if(! r.headers.exists("Host"))
        return err("Missing Host field");
    //if(r.headers["Upgrade"] != "websocket")...
    return {};
}

template<class Stream>
void
socket<Stream>::do_response(
    beast::deprecated_http::message const& m, error_code& ec)
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
socket<Stream>::do_read_fh(
    detail::frame_streambuf& fb,
        close::value& code, error_code& ec)
{
    fb.commit(boost::asio::read(
        stream_, fb.prepare(2), ec));
    if(ec)
        return;
    auto const n = detail::read_fh1(
        rd_fh_, fb, role_, code);
    if(code)
        return;
    if(n > 0)
    {
        fb.commit(boost::asio::read(
            stream_, fb.prepare(n), ec));
        if(ec)
            return;
    }
    detail::read_fh2(
        rd_fh_, fb, role_, code);
    if(code)
        return;
    prepare_fh(code);
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
auto
async_read(socket<Stream>& ws, opcode::value& op,
    Streambuf& streambuf, ReadHandler&& handler)
{
    static_assert(beast::is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    beast::async_completion<
        ReadHandler, void(error_code)
            > completion(handler);
    detail::read_op<Stream, Streambuf, decltype(
        completion.handler)>{completion.handler,
            ws, op, streambuf};
    return completion.result.get();
}

template<class Stream, class ConstBufferSequence>
void
write_msg(socket<Stream>& ws, opcode::value op,
    ConstBufferSequence const& bs, error_code& ec)
{
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    ws.write(op, true, bs, ec);
}

template<class Stream,
    class ConstBufferSequence, class WriteHandler>
auto
async_write(socket<Stream>& ws, opcode::value op,
    ConstBufferSequence const& bs, WriteHandler&& handler)
{
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    return ws.async_write(op, true, bs,
        std::forward<WriteHandler>(handler));
}

} // wsproto
} // beast

#endif
