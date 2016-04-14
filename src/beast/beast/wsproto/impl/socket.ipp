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
#include <beast/wsproto/impl/response_op.ipp>
#include <beast/wsproto/impl/write_op.ipp>
#include <beast/asio/append_buffers.h>
#include <beast/asio/async_completion.h>
#include <beast/asio/prepare_buffers.h>
#include <beast/asio/read_until.h>
#include <beast/asio/static_streambuf.h>
#include <beast/asio/streambuf.h>
#include <beast/asio/type_check.h>
#include <beast/http/read.h>
#include <beast/http/write.h>
#include <beast/http/reason.h>
#include <beast/http/rfc2616.h>
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
    opcode op, ping_payload_type const& data)
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
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    stream_.buffer().commit(buffer_copy(
        stream_.buffer().prepare(
            buffer_size(buffers)), buffers));
    http::parsed_request<http::empty_body> m;
    http::read(next_layer_, stream_.buffer(), m, ec);
    if(ec)
        return;
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
template<class Body, class Allocator>
void
socket<Stream>::accept(
    http::parsed_request<Body, Allocator> const& request)
{
    error_code ec;
    accept(request, ec);
    detail::maybe_throw(ec, "accept");
}

template<class Stream>
template<class Body, class Allocator>
void
socket<Stream>::accept(
    http::parsed_request<Body, Allocator> const& req,
        error_code& ec)
{
    auto resp = build_response(req);
    http::write(stream_, resp, ec);
    if(resp.status != 101)
    {
        ec = error::handshake_failed;
        // VFALCO TODO Respect keep alive setting, perform
        //             teardown if Connection: close.
        return;
    }
    role_ = role_type::server;
}

template<class Stream>
template<class Body, class Allocator, class AcceptHandler>
auto
socket<Stream>::async_accept(
    http::parsed_request<Body, Allocator> const& req,
        AcceptHandler&& handler)
{
    beast::async_completion<
        AcceptHandler, void(error_code)
            > completion(handler);
    response_op<decltype(completion.handler)>{
        completion.handler, *this, req, false};
    return completion.result.get();
}

template<class Stream>
void
socket<Stream>::handshake(std::string const& host,
    std::string const& resource, error_code& ec)
{
    std::string key;
    http::write(stream_,
        build_request(host, resource, key), ec);
    if(ec)
        return;
    http::parsed_response<http::string_body> resp;
    http::read(next_layer_, stream_.buffer(), resp, ec);
    if(ec)
        return;
    do_response(resp, key, ec);
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
socket<Stream>::write(opcode op, bool fin,
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
socket<Stream>::async_write(opcode op, bool fin,
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
http::prepared_request<http::empty_body>
socket<Stream>::build_request(std::string const& host,
    std::string const& resource, std::string& key)
{
    http::request<http::empty_body> req;
    req.url = "/";
    req.version = 11;
    req.method = http::method_t::http_get;
    req.headers.insert("Host", host);
    req.headers.insert("Upgrade", "websocket");
    key = detail::make_sec_ws_key(maskgen_);
    req.headers.insert("Sec-WebSocket-Key", key);
    req.headers.insert("Sec-WebSocket-Version", "13");
    (*d_)(req);
    return http::prepare(req,
        http::connection(http::upgrade));
}

template<class Stream>
template<class Body, class Allocator>
http::prepared_response<http::string_body>
socket<Stream>::build_response(
    http::parsed_request<Body, Allocator> const& req)
{
    auto err =
        [&](auto const& text)
        {
            http::response<http::string_body> resp(
                {400, http::reason_string(400), req.version});
            resp.body = text;
            return http::prepare(std::move(resp), req,
                http::connection(keep_alive_));
        };
    if(req.version < 11)
        return err("HTTP version 1.1 required");
    if(req.method != http::method_t::http_get)
        return err("Wrong method");
    if(! req.upgrade)
        return err("Expected Upgrade request");
    if(! req.headers.exists("Host"))
        return err("Missing Host");
    if(! req.headers.exists("Sec-WebSocket-Key"))
        return err("Missing Sec-WebSocket-Key");
    {
        auto const version =
            req.headers["Sec-WebSocket-Version"];
        if(version.empty())
            return err("Missing Sec-WebSocket-Version");
        if(version != "13")
            return err("Unsupported Sec-WebSocket-Version");
    }
    if(! rfc2616::token_in_list(
            req.headers["Upgrade"], "websocket"))
        return err("Missing websocket Upgrade token");
    http::response<http::string_body> resp(
        {101, http::reason_string(101), req.version});
    resp.headers.insert("Upgrade", "websocket");
    {
        auto const key =
            req.headers["Sec-WebSocket-Key"];
        resp.headers.insert("Sec-WebSocket-Key", key);
        resp.headers.insert("Sec-WebSocket-Accept",
            detail::make_sec_ws_accept(key));
    }
    resp.headers.replace("Server", "Beast.WSProto");
    (*d_)(resp);
    return http::prepare(std::move(resp), req,
        http::connection(http::upgrade));
}

template<class Stream>
template<class Body, class Allocator>
void
socket<Stream>::do_response(
    http::parsed_response<Body, Allocator> const& resp,
        std::string const& key, error_code& ec)
{
    // VFALCO Review these error codes
    auto fail = [&]{ ec = error::response_failed; };
    if(resp.status != 101)
        return fail();
    if(! resp.upgrade)
        return fail();
    if(! rfc2616::ci_equal(
            resp.headers["Upgrade"], "websocket"))
        return fail();
    if(! resp.headers.exists("Sec-WebSocket-Accept"))
        return fail();
    if(resp.headers["Sec-WebSocket-Accept"] !=
        detail::make_sec_ws_accept(key))
        return fail();
    role_ = role_type::client;
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
read(socket<Stream>& ws, opcode& op,
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
async_read(socket<Stream>& ws, opcode& op,
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
write_msg(socket<Stream>& ws, opcode op,
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
async_write(socket<Stream>& ws, opcode op,
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
