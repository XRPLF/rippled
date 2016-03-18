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

#ifndef BEAST_WSPROTO_IMPL_STREAM_IPP_INCLUDED
#define BEAST_WSPROTO_IMPL_STREAM_IPP_INCLUDED

#include <beast/wsproto/detail/hybi13.h>
#include <beast/asio/streambuf.h>

namespace beast {
namespace wsproto {

namespace detail {

template<class _>
error_code
stream_base::process_fh()
{
    error_code ec;
    
    if(ec = detail::validate_fh(role_, rs_.fh))
        return ec;

    // continuation without an active message
    if(! rs_.cont && rs_.fh.op == opcode::cont)
        return error::frame_header_invalid;

    // new data frame when continuation expected
    if(rs_.cont && ! is_control(rs_.fh.op) &&
            rs_.fh.op != opcode::cont)
        return error::frame_header_invalid;

    if(! is_control(rs_.fh.op))
    {
        if(rs_.fh.mask)
            prepare_key(rs_.key, rs_.fh.key);
        if(rs_.fh.op != opcode::cont)
        {
            rs_.need = rs_.fh.len;
            rs_.text = rs_.fh.op == opcode::text;
        }
        rs_.cont = ! rs_.fh.fin;
    }

    return ec;
}

} // detail

template<class Stream>
template<class... Args>
stream<Stream>::stream(Args&&... args)
    : stream_(std::forward<Args>(args)...)
{
    decorate([](auto const&) { });
}

template<class Stream>
void
stream<Stream>::upgrade(std::string const& host,
    std::string const& resource, error_code& ec)
{
    // VFALCO Used for tests, not production quality
    using namespace boost::asio;
    {
        auto m = make_upgrade(host, resource);
        beast::asio::streambuf sb;
        beast::http::write(sb, m);
        boost::asio::write(stream_, sb.data(), ec);
        if(ec)
            return;
    }
    {
        streambuf sb;
        read_until(stream_, sb, "\r\n\r\n");
        using namespace beast;
        http::body b;
        http::message m;
        http::parser p(m, b, false);
        auto const result = p.write(sb.data());
        if (result.first || ! p.complete())
            throw std::runtime_error(result.first.message());
        sb.consume(result.second);
    }
    role_ = role_type::client;;
}

template<class Stream>
template<class Handler>
void
stream<Stream>::async_upgrade(std::string const& host,
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
void
stream<Stream>::accept(
    beast::http::message const& m, error_code& ec)
{
    role_ = role_type::server;
    // TODO
    throw std::runtime_error("unimplemented");
}

template<class Stream>
template<class Handler>
void
stream<Stream>::async_accept(
    beast::http::message const& m, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    error_code ec = do_accept(m);
    if(ec)
    {
        auto sb = write_error_response(ec);
        return get_io_service().dispatch(
            detail::streambuf_op<Stream, decltype(sb),
                std::decay_t<Handler>>{stream_, std::move(sb),
                    ec, std::forward<Handler>(h)});
    }
    role_ = role_type::server;
    auto sb = make_response(m);
    get_io_service().dispatch(
        detail::streambuf_op<Stream, decltype(sb),
            std::decay_t<Handler>>{stream_, std::move(sb),
                ec, std::forward<Handler>(h)});
}

template<class Stream>
void
stream<Stream>::read_fh(frame_header& fh, error_code& ec)
{
    detail::fh_buffer buf;
    using namespace boost::asio;
    read(stream_, buffer(buf.data(), 2), ec);
    if(ec)
        return;
    read(stream_, mutable_buffers_1(buf.data() + 2,
        detail::decode_fh1(fh, buf)), ec);
    if(ec)
        return;
    ec = detail::decode_fh2(fh, buf);
    if(ec)
        return;
    ec = process_fh();
}

template<class Stream>
template<class Handler>
void
stream<Stream>::async_read_fh(frame_header& fh, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    get_io_service().dispatch(read_fh_op<
        std::decay_t<Handler>>{*this, fh,
            std::forward<Handler>(h)});
}

template<class Stream>
template<class Buffers, class Handler>
void
stream<Stream>::async_read_some(
    Buffers&& b, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code, std::size_t)>::value,
            "Type does not meet the handler requirements");
    get_io_service().dispatch(read_some_op<
        std::decay_t<Buffers>, std::decay_t<Handler>>{
            *this, std::forward<Buffers>(b),
                std::forward<Handler>(h)});
}

template<class Stream>
template<class ConstBufferSequence>
void
stream<Stream>::write(opcode::value op, bool fin,
    ConstBufferSequence const& buffers, error_code& ec)
{
    frame_header fh;
    fh.op = op;
    fh.fin = fin;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = buffer_size(buffers);
    if((fh.mask = (role_ == role_type::client)))
        fh.key = maskgen_();
    if(fh.mask)
    {
        beast::asio::streambuf sb;
        wsproto::detail::write_fh(sb, fh);
        wsproto::detail::write_body(sb, fh, buffers);
        boost::asio::write(stream_, sb.data());
    }
    else
    {
        // TODO use append_buffers
        beast::asio::streambuf sb;
        wsproto::detail::write_fh(sb, fh);
        wsproto::detail::write_body(sb, fh, buffers);
        boost::asio::write(stream_, sb.data());
    }
}

template<class Stream>
template<class ConstBuffers, class WriteHandler>
void
stream<Stream>::async_write(opcode::value op, bool fin,
    ConstBuffers const& b, WriteHandler&& h)
{
    static_assert(beast::is_call_possible<WriteHandler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    frame_header fh;
    fh.op = op;
    fh.fin = fin;
    fh.rsv1 = 0;
    fh.rsv2 = 0;
    fh.rsv3 = 0;
    fh.len = boost::asio::buffer_size(b);
    if((fh.mask = (role_ == role_type::client)))
        fh.key = maskgen_();
    get_io_service().dispatch(write_op<
        std::decay_t<WriteHandler>>{*this,
            fh, b, std::forward<WriteHandler>(h)});
}

template<class Stream>
beast::asio::streambuf
stream<Stream>::write_error_response(error_code const& ec)
{
    beast::http::message m;
    std::string const body = ec.message();
    m.request(false);
    m.version(beast::http::http_1_1());
    m.status(400);
    m.reason("Bad request");
    m.headers.append("Connection", "close"); // VFALCO ?
    m.headers.append("Content-Type", "text/html");
    m.headers.append("Content-Length",
        std::to_string(body.size()));
    (*decorate_)(m);
    beast::asio::streambuf sb;
    beast::http::write(sb, m);
    return sb;
}

template<class Stream>
beast::http::message
stream<Stream>::make_upgrade(std::string const& host,
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
stream<Stream>::make_response(
    beast::http::message const& r)
{
    beast::http::message m;
    m.request(false);
    m.status(101);
    m.reason("Switching Protocols");
    m.version(beast::http::http_1_1());
    m.headers.append("Connection", "upgrade");
    m.headers.append("Upgrade", "websocket");
    auto const key =
        r.headers["Sec-WebSocket-Key"];
    m.headers.append("Sec-WebSocket-Key", key);
    m.headers.append("Sec-WebSocket-Accept",
        detail::make_sec_ws_accept(key));
    (*decorate_)(m);
    beast::asio::streambuf sb;
    beast::http::write(sb, m);
    return sb;
}

template<class Stream>
error_code
stream<Stream>::do_accept(beast::http::message const& r)
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

template<class Stream, class Streambuf, class Handler>
void
async_read_msg(stream<Stream>& ws, Streambuf& sb, Handler&& h)
{
    static_assert(beast::is_call_possible<Handler,
        void(error_code)>::value,
            "Type does not meet the handler requirements");
    return ws.get_io_service().dispatch(
        detail::read_msg_op<Stream, Streambuf,
            std::decay_t<Handler>>{ws, sb,
                std::forward<Handler>(h)});
}

} // wsproto
} // beast

#endif
