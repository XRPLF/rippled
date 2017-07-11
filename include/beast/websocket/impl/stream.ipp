//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_STREAM_IPP
#define BEAST_WEBSOCKET_IMPL_STREAM_IPP

#include <beast/websocket/rfc6455.hpp>
#include <beast/websocket/teardown.hpp>
#include <beast/websocket/detail/hybi13.hpp>
#include <beast/websocket/detail/pmd_extension.hpp>
#include <beast/version.hpp>
#include <beast/http/read.hpp>
#include <beast/http/write.hpp>
#include <beast/http/rfc7230.hpp>
#include <beast/core/buffer_cat.hpp>
#include <beast/core/buffer_prefix.hpp>
#include <beast/core/consuming_buffers.hpp>
#include <beast/core/static_buffer.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/assert.hpp>
#include <boost/endian/buffers.hpp>
#include <boost/make_unique.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

#include <iostream>

namespace beast {
namespace websocket {

template<class NextLayer>
template<class... Args>
stream<NextLayer>::
stream(Args&&... args)
    : stream_(std::forward<Args>(args)...)
{
}

template<class NextLayer>
void
stream<NextLayer>::
set_option(permessage_deflate const& o)
{
    if( o.server_max_window_bits > 15 ||
        o.server_max_window_bits < 9)
        BOOST_THROW_EXCEPTION(std::invalid_argument{
            "invalid server_max_window_bits"});
    if( o.client_max_window_bits > 15 ||
        o.client_max_window_bits < 9)
        BOOST_THROW_EXCEPTION(std::invalid_argument{
            "invalid client_max_window_bits"});
    if( o.compLevel < 0 ||
        o.compLevel > 9)
        BOOST_THROW_EXCEPTION(std::invalid_argument{
            "invalid compLevel"});
    if( o.memLevel < 1 ||
        o.memLevel > 9)
        BOOST_THROW_EXCEPTION(std::invalid_argument{
            "invalid memLevel"});
    pmd_opts_ = o;
}

//------------------------------------------------------------------------------

template<class NextLayer>
void
stream<NextLayer>::
reset()
{
    failed_ = false;
    rd_.cont = false;
    wr_close_ = false;
    wr_.cont = false;
    wr_block_ = nullptr;    // should be nullptr on close anyway
    ping_data_ = nullptr;   // should be nullptr on close anyway

    stream_.buffer().consume(
        stream_.buffer().size());
}

template<class NextLayer>
template<class Decorator>
void
stream<NextLayer>::
do_accept(
    Decorator const& decorator, error_code& ec)
{
    http::request_parser<http::empty_body> p;
    http::read_header(next_layer(),
        stream_.buffer(), p, ec);
    if(ec)
        return;
    do_accept(p.get(), decorator, ec);
}

template<class NextLayer>
template<class Allocator, class Decorator>
void
stream<NextLayer>::
do_accept(http::header<true,
    http::basic_fields<Allocator>> const& req,
        Decorator const& decorator, error_code& ec)
{
    auto const res = build_response(req, decorator);
    http::write(stream_, res, ec);
    if(ec)
        return;
    if(res.result() != http::status::switching_protocols)
    {
        ec = error::handshake_failed;
        // VFALCO TODO Respect keep alive setting, perform
        //             teardown if Connection: close.
        return;
    }
    pmd_read(pmd_config_, req);
    open(role_type::server);
}

template<class NextLayer>
template<class RequestDecorator>
void
stream<NextLayer>::
do_handshake(response_type* res_p,
    string_view host,
        string_view target,
            RequestDecorator const& decorator,
                error_code& ec)
{
    response_type res;
    reset();
    detail::sec_ws_key_type key;
    {
        auto const req = build_request(
            key, host, target, decorator);
        pmd_read(pmd_config_, req);
        http::write(stream_, req, ec);
    }
    if(ec)
        return;
    http::read(next_layer(), stream_.buffer(), res, ec);
    if(ec)
        return;
    do_response(res, key, ec);
    if(res_p)
        *res_p = std::move(res);
}

template<class NextLayer>
template<class Decorator>
request_type
stream<NextLayer>::
build_request(detail::sec_ws_key_type& key,
    string_view host,
        string_view target,
            Decorator const& decorator)
{
    request_type req;
    req.target(target);
    req.version = 11;
    req.method(http::verb::get);
    req.set(http::field::host, host);
    req.set(http::field::upgrade, "websocket");
    req.set(http::field::connection, "upgrade");
    detail::make_sec_ws_key(key, maskgen_);
    req.set(http::field::sec_websocket_key, key);
    req.set(http::field::sec_websocket_version, "13");
    if(pmd_opts_.client_enable)
    {
        detail::pmd_offer config;
        config.accept = true;
        config.server_max_window_bits =
            pmd_opts_.server_max_window_bits;
        config.client_max_window_bits =
            pmd_opts_.client_max_window_bits;
        config.server_no_context_takeover =
            pmd_opts_.server_no_context_takeover;
        config.client_no_context_takeover =
            pmd_opts_.client_no_context_takeover;
        detail::pmd_write(req, config);
    }
    decorator(req);
    if(! req.count(http::field::user_agent))
        req.set(http::field::user_agent,
            BEAST_VERSION_STRING);
    return req;
}

template<class NextLayer>
template<class Allocator, class Decorator>
response_type
stream<NextLayer>::
build_response(http::header<true,
    http::basic_fields<Allocator>> const& req,
        Decorator const& decorator)
{
    auto const decorate =
        [&decorator](response_type& res)
        {
            decorator(res);
            if(! res.count(http::field::server))
            {
                BOOST_STATIC_ASSERT(sizeof(BEAST_VERSION_STRING) < 20);
                static_string<20> s(BEAST_VERSION_STRING);
                res.set(http::field::server, s);
            }
        };
    auto err =
        [&](std::string const& text)
        {
            response_type res;
            res.version = req.version;
            res.result(http::status::bad_request);
            res.body = text;
            res.prepare_payload();
            decorate(res);
            return res;
        };
    if(req.version < 11)
        return err("HTTP version 1.1 required");
    if(req.method() != http::verb::get)
        return err("Wrong method");
    if(! is_upgrade(req))
        return err("Expected Upgrade request");
    if(! req.count(http::field::host))
        return err("Missing Host");
    if(! req.count(http::field::sec_websocket_key))
        return err("Missing Sec-WebSocket-Key");
    if(! http::token_list{req[http::field::upgrade]}.exists("websocket"))
        return err("Missing websocket Upgrade token");
    auto const key = req[http::field::sec_websocket_key];
    if(key.size() > detail::sec_ws_key_type::max_size_n)
        return err("Invalid Sec-WebSocket-Key");
    {
        auto const version =
            req[http::field::sec_websocket_version];
        if(version.empty())
            return err("Missing Sec-WebSocket-Version");
        if(version != "13")
        {
            response_type res;
            res.result(http::status::upgrade_required);
            res.version = req.version;
            res.set(http::field::sec_websocket_version, "13");
            res.prepare_payload();
            decorate(res);
            return res;
        }
    }

    response_type res;
    {
        detail::pmd_offer offer;
        detail::pmd_offer unused;
        pmd_read(offer, req);
        pmd_negotiate(res, unused, offer, pmd_opts_);
    }
    res.result(http::status::switching_protocols);
    res.version = req.version;
    res.set(http::field::upgrade, "websocket");
    res.set(http::field::connection, "upgrade");
    {
        detail::sec_ws_accept_type acc;
        detail::make_sec_ws_accept(acc, key);
        res.set(http::field::sec_websocket_accept, acc);
    }
    decorate(res);
    return res;
}

template<class NextLayer>
void
stream<NextLayer>::
do_response(http::header<false> const& res,
    detail::sec_ws_key_type const& key, error_code& ec)
{
    bool const success = [&]()
    {
        if(res.version < 11)
            return false;
        if(res.result() != http::status::switching_protocols)
            return false;
        if(! http::token_list{res[http::field::connection]}.exists("upgrade"))
            return false;
        if(! http::token_list{res[http::field::upgrade]}.exists("websocket"))
            return false;
        if(res.count(http::field::sec_websocket_accept) != 1)
            return false;
        detail::sec_ws_accept_type acc;
        detail::make_sec_ws_accept(acc, key);
        if(acc.compare(
                res[http::field::sec_websocket_accept]) != 0)
            return false;
        return true;
    }();
    if(! success)
    {
        ec = error::handshake_failed;
        return;
    }
    ec.assign(0, ec.category());
    detail::pmd_offer offer;
    pmd_read(offer, res);
    // VFALCO see if offer satisfies pmd_config_,
    //        return an error if not.
    pmd_config_ = offer; // overwrite for now
    open(role_type::client);
}

//------------------------------------------------------------------------------

template<class NextLayer>
void
stream<NextLayer>::
open(role_type role)
{
    // VFALCO TODO analyze and remove dupe code in reset()
    role_ = role;
    failed_ = false;
    rd_.cont = false;
    wr_close_ = false;
    wr_block_ = nullptr;    // should be nullptr on close anyway
    ping_data_ = nullptr;   // should be nullptr on close anyway

    wr_.cont = false;
    wr_.buf_size = 0;

    if(((role_ == role_type::client && pmd_opts_.client_enable) ||
        (role_ == role_type::server && pmd_opts_.server_enable)) &&
            pmd_config_.accept)
    {
        pmd_normalize(pmd_config_);
        pmd_.reset(new pmd_t);
        if(role_ == role_type::client)
        {
            pmd_->zi.reset(
                pmd_config_.server_max_window_bits);
            pmd_->zo.reset(
                pmd_opts_.compLevel,
                pmd_config_.client_max_window_bits,
                pmd_opts_.memLevel,
                zlib::Strategy::normal);
        }
        else
        {
            pmd_->zi.reset(
                pmd_config_.client_max_window_bits);
            pmd_->zo.reset(
                pmd_opts_.compLevel,
                pmd_config_.server_max_window_bits,
                pmd_opts_.memLevel,
                zlib::Strategy::normal);
        }
    }
}

template<class NextLayer>
void
stream<NextLayer>::
close()
{
    rd_.buf.reset();
    wr_.buf.reset();
    pmd_.reset();
}

// Read fixed frame header from buffer
// Requires at least 2 bytes
//
template<class NextLayer>
template<class DynamicBuffer>
std::size_t
stream<NextLayer>::
read_fh1(detail::frame_header& fh,
    DynamicBuffer& db, close_code& code)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    auto const err =
        [&](close_code cv)
        {
            code = cv;
            return 0;
        };
    std::uint8_t b[2];
    BOOST_ASSERT(buffer_size(db.data()) >= sizeof(b));
    db.consume(buffer_copy(buffer(b), db.data()));
    std::size_t need;
    fh.len = b[1] & 0x7f;
    switch(fh.len)
    {
        case 126: need = 2; break;
        case 127: need = 8; break;
        default:
            need = 0;
    }
    fh.mask = (b[1] & 0x80) != 0;
    if(fh.mask)
        need += 4;
    fh.op   = static_cast<
        detail::opcode>(b[0] & 0x0f);
    fh.fin  = (b[0] & 0x80) != 0;
    fh.rsv1 = (b[0] & 0x40) != 0;
    fh.rsv2 = (b[0] & 0x20) != 0;
    fh.rsv3 = (b[0] & 0x10) != 0;
    switch(fh.op)
    {
    case detail::opcode::binary:
    case detail::opcode::text:
        if(rd_.cont)
        {
            // new data frame when continuation expected
            return err(close_code::protocol_error);
        }
        if((fh.rsv1 && ! pmd_) ||
            fh.rsv2 || fh.rsv3)
        {
            // reserved bits not cleared
            return err(close_code::protocol_error);
        }
        if(pmd_)
            pmd_->rd_set = fh.rsv1;
        break;

    case detail::opcode::cont:
        if(! rd_.cont)
        {
            // continuation without an active message
            return err(close_code::protocol_error);
        }
        if(fh.rsv1 || fh.rsv2 || fh.rsv3)
        {
            // reserved bits not cleared
            return err(close_code::protocol_error);
        }
        break;

    default:
        if(is_reserved(fh.op))
        {
            // reserved opcode
            return err(close_code::protocol_error);
        }
        if(! fh.fin)
        {
            // fragmented control message
            return err(close_code::protocol_error);
        }
        if(fh.len > 125)
        {
            // invalid length for control message
            return err(close_code::protocol_error);
        }
        if(fh.rsv1 || fh.rsv2 || fh.rsv3)
        {
            // reserved bits not cleared
            return err(close_code::protocol_error);
        }
        break;
    }
    // unmasked frame from client
    if(role_ == role_type::server && ! fh.mask)
    {
        code = close_code::protocol_error;
        return 0;
    }
    // masked frame from server
    if(role_ == role_type::client && fh.mask)
    {
        code = close_code::protocol_error;
        return 0;
    }
    code = close_code::none;
    return need;
}

// Decode variable frame header from buffer
//
template<class NextLayer>
template<class DynamicBuffer>
void
stream<NextLayer>::
read_fh2(detail::frame_header& fh,
    DynamicBuffer& db, close_code& code)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    using namespace boost::endian;
    switch(fh.len)
    {
    case 126:
    {
        std::uint8_t b[2];
        BOOST_ASSERT(buffer_size(db.data()) >= sizeof(b));
        db.consume(buffer_copy(buffer(b), db.data()));
        fh.len = detail::big_uint16_to_native(&b[0]);
        // length not canonical
        if(fh.len < 126)
        {
            code = close_code::protocol_error;
            return;
        }
        break;
    }
    case 127:
    {
        std::uint8_t b[8];
        BOOST_ASSERT(buffer_size(db.data()) >= sizeof(b));
        db.consume(buffer_copy(buffer(b), db.data()));
        fh.len = detail::big_uint64_to_native(&b[0]);
        // length not canonical
        if(fh.len < 65536)
        {
            code = close_code::protocol_error;
            return;
        }
        break;
    }
    }
    if(fh.mask)
    {
        std::uint8_t b[4];
        BOOST_ASSERT(buffer_size(db.data()) >= sizeof(b));
        db.consume(buffer_copy(buffer(b), db.data()));
        fh.key = detail::little_uint32_to_native(&b[0]);
    }
    else
    {
        // initialize this otherwise operator== breaks
        fh.key = 0;
    }
    if(! is_control(fh.op))
    {
        if(fh.op != detail::opcode::cont)
        {
            rd_.size = 0;
            rd_.op = fh.op;
        }
        else
        {
            if(rd_.size > (std::numeric_limits<
                std::uint64_t>::max)() - fh.len)
            {
                code = close_code::too_big;
                return;
            }
        }
        rd_.cont = ! fh.fin;
    }
    code = close_code::none;
}

template<class NextLayer>
void
stream<NextLayer>::
rd_begin()
{
    // Maintain the read buffer
    if(pmd_)
    {
        if(! rd_.buf || rd_.buf_size != rd_buf_size_)
        {
            rd_.buf_size = rd_buf_size_;
            rd_.buf = boost::make_unique_noinit<
                std::uint8_t[]>(rd_.buf_size);
        }
    }
}

template<class NextLayer>
void
stream<NextLayer>::
wr_begin()
{
    wr_.autofrag = wr_autofrag_;
    wr_.compress = static_cast<bool>(pmd_);

    // Maintain the write buffer
    if( wr_.compress ||
        role_ == role_type::client)
    {
        if(! wr_.buf || wr_.buf_size != wr_buf_size_)
        {
            wr_.buf_size = wr_buf_size_;
            wr_.buf = boost::make_unique_noinit<
                std::uint8_t[]>(wr_.buf_size);
        }
    }
    else
    {
        wr_.buf_size = wr_buf_size_;
        wr_.buf.reset();
    }
}

template<class NextLayer>
template<class DynamicBuffer>
void
stream<NextLayer>::
write_close(DynamicBuffer& db, close_reason const& cr)
{
    using namespace boost::endian;
    detail::frame_header fh;
    fh.op = detail::opcode::close;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = cr.code == close_code::none ?
        0 : 2 + cr.reason.size();
    fh.mask = role_ == role_type::client;
    if(fh.mask)
        fh.key = maskgen_();
    detail::write(db, fh);
    if(cr.code != close_code::none)
    {
        detail::prepared_key key;
        if(fh.mask)
            detail::prepare_key(key, fh.key);
        {
            std::uint8_t b[2];
            ::new(&b[0]) big_uint16_buf_t{
                (std::uint16_t)cr.code};
            auto d = db.prepare(2);
            boost::asio::buffer_copy(d,
                boost::asio::buffer(b));
            if(fh.mask)
                detail::mask_inplace(d, key);
            db.commit(2);
        }
        if(! cr.reason.empty())
        {
            auto d = db.prepare(cr.reason.size());
            boost::asio::buffer_copy(d,
                boost::asio::const_buffer(
                    cr.reason.data(), cr.reason.size()));
            if(fh.mask)
                detail::mask_inplace(d, key);
            db.commit(cr.reason.size());
        }
    }
}

template<class NextLayer>
template<class DynamicBuffer>
void
stream<NextLayer>::
write_ping(DynamicBuffer& db,
    detail::opcode code, ping_data const& data)
{
    detail::frame_header fh;
    fh.op = code;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = data.size();
    fh.mask = role_ == role_type::client;
    if(fh.mask)
        fh.key = maskgen_();
    detail::write(db, fh);
    if(data.empty())
        return;
    detail::prepared_key key;
    if(fh.mask)
        detail::prepare_key(key, fh.key);
    auto d = db.prepare(data.size());
    boost::asio::buffer_copy(d,
        boost::asio::const_buffers_1(
            data.data(), data.size()));
    if(fh.mask)
        detail::mask_inplace(d, key);
    db.commit(data.size());
}

} // websocket
} // beast

#endif
