//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP
#define BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP

#include <beast/websocket/error.hpp>
#include <beast/websocket/option.hpp>
#include <beast/websocket/rfc6455.hpp>
#include <beast/websocket/detail/decorator.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <beast/websocket/detail/invokable.hpp>
#include <beast/websocket/detail/mask.hpp>
#include <beast/websocket/detail/pmd_extension.hpp>
#include <beast/websocket/detail/utf8_checker.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/string_body.hpp>
#include <beast/zlib/deflate_stream.hpp>
#include <beast/zlib/inflate_stream.hpp>
#include <boost/asio/error.hpp>
#include <boost/assert.hpp>
#include <cstdint>
#include <memory>

namespace beast {
namespace websocket {
namespace detail {

/// Identifies the role of a WebSockets stream.
enum class role_type
{
    /// Stream is operating as a client.
    client,

    /// Stream is operating as a server.
    server
};

//------------------------------------------------------------------------------

struct stream_base
{
protected:
    friend class frame_test;

    struct op {};

    detail::maskgen maskgen_;               // source of mask keys
    decorator_type d_;                      // adorns http messages
    bool keep_alive_ = false;               // close on failed upgrade
    std::size_t rd_msg_max_ =
        16 * 1024 * 1024;                   // max message size
    bool wr_autofrag_ = true;               // auto fragment
    std::size_t wr_buf_size_ = 4096;        // write buffer size
    std::size_t rd_buf_size_ = 4096;        // read buffer size
    opcode wr_opcode_ = opcode::text;       // outgoing message type
    pong_cb pong_cb_;                       // pong callback
    role_type role_;                        // server or client
    bool failed_;                           // the connection failed

    bool wr_close_;                         // sent close frame
    op* wr_block_;                          // op currenly writing

    ping_data* pong_data_;                  // where to put pong payload
    invokable rd_op_;                       // invoked after write completes
    invokable wr_op_;                       // invoked after read completes
    close_reason cr_;                       // set from received close frame

    // State information for the message being received
    //
    struct rd_t
    {
        // opcode of current message being read
        opcode op;

        // `true` if the next frame is a continuation.
        bool cont;

        // Checks that test messages are valid utf8
        detail::utf8_checker utf8;

        // Size of the current message so far.
        std::uint64_t size;

        // Size of the read buffer.
        // This gets set to the read buffer size option at the
        // beginning of sending a message, so that the option can be
        // changed mid-send without affecting the current message.
        std::size_t buf_size;

        // The read buffer. Used for compression and masking.
        std::unique_ptr<std::uint8_t[]> buf;
    };

    rd_t rd_;

    // State information for the message being sent
    //
    struct wr_t
    {
        // `true` if next frame is a continuation,
        // `false` if next frame starts a new message
        bool cont;

        // `true` if this message should be auto-fragmented
        // This gets set to the auto-fragment option at the beginning
        // of sending a message, so that the option can be changed
        // mid-send without affecting the current message.
        bool autofrag;

        // `true` if this message should be compressed.
        // This gets set to the compress option at the beginning of
        // of sending a message, so that the option can be changed
        // mid-send without affecting the current message.
        bool compress;

        // Size of the write buffer.
        // This gets set to the write buffer size option at the
        // beginning of sending a message, so that the option can be
        // changed mid-send without affecting the current message.
        std::size_t buf_size;

        // The write buffer. Used for compression and masking.
        // The buffer is allocated or reallocated at the beginning of
        // sending a message.
        std::unique_ptr<std::uint8_t[]> buf;
    };

    wr_t wr_;

    // State information for the permessage-deflate extension
    struct pmd_t
    {
        // `true` if current read message is compressed
        bool rd_set;

        zlib::deflate_stream zo;
        zlib::inflate_stream zi;
    };

    // If not engaged, then permessage-deflate is not
    // enabled for the currently active session.
    std::unique_ptr<pmd_t> pmd_;

    // Local options for permessage-deflate
    permessage_deflate pmd_opts_;

    // Offer for clients, negotiated result for servers
    pmd_offer pmd_config_;

    stream_base(stream_base&&) = default;
    stream_base(stream_base const&) = delete;
    stream_base& operator=(stream_base&&) = default;
    stream_base& operator=(stream_base const&) = delete;

    stream_base()
        : d_(detail::default_decorator{})
    {
    }

    template<class = void>
    void
    open(role_type role);

    template<class = void>
    void
    close();

    template<class DynamicBuffer>
    std::size_t
    read_fh1(detail::frame_header& fh,
        DynamicBuffer& db, close_code::value& code);

    template<class DynamicBuffer>
    void
    read_fh2(detail::frame_header& fh,
        DynamicBuffer& db, close_code::value& code);

    // Called before receiving the first frame of each message
    template<class = void>
    void
    rd_begin();

    // Called before sending the first frame of each message
    //
    template<class = void>
    void
    wr_begin();

    template<class DynamicBuffer>
    void
    write_close(DynamicBuffer& db, close_reason const& rc);

    template<class DynamicBuffer>
    void
    write_ping(DynamicBuffer& db, opcode op, ping_data const& data);
};

template<class>
void
stream_base::
open(role_type role)
{
    // VFALCO TODO analyze and remove dupe code in reset()
    role_ = role;
    failed_ = false;
    rd_.cont = false;
    wr_close_ = false;
    wr_block_ = nullptr;    // should be nullptr on close anyway
    pong_data_ = nullptr;   // should be nullptr on close anyway

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

template<class>
void
stream_base::
close()
{
    rd_.buf.reset();
    wr_.buf.reset();
    pmd_.reset();
}

// Read fixed frame header from buffer
// Requires at least 2 bytes
//
template<class DynamicBuffer>
std::size_t
stream_base::
read_fh1(detail::frame_header& fh,
    DynamicBuffer& db, close_code::value& code)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    auto const err =
        [&](close_code::value cv)
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
    fh.op   = static_cast<opcode>(b[0] & 0x0f);
    fh.fin  = (b[0] & 0x80) != 0;
    fh.rsv1 = (b[0] & 0x40) != 0;
    fh.rsv2 = (b[0] & 0x20) != 0;
    fh.rsv3 = (b[0] & 0x10) != 0;
    switch(fh.op)
    {
    case opcode::binary:
    case opcode::text:
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

    case opcode::cont:
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
template<class DynamicBuffer>
void
stream_base::
read_fh2(detail::frame_header& fh,
    DynamicBuffer& db, close_code::value& code)
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
        fh.len = big_uint16_to_native(&b[0]);
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
        fh.len = big_uint64_to_native(&b[0]);
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
        fh.key = little_uint32_to_native(&b[0]);
    }
    else
    {
        // initialize this otherwise operator== breaks
        fh.key = 0;
    }
    if(! is_control(fh.op))
    {
        if(fh.op != opcode::cont)
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

template<class>
void
stream_base::
rd_begin()
{
    // Maintain the read buffer
    if(pmd_)
    {
        if(! rd_.buf || rd_.buf_size != rd_buf_size_)
        {
            rd_.buf_size = rd_buf_size_;
            rd_.buf.reset(new std::uint8_t[rd_.buf_size]);
        }
    }
}

template<class>
void
stream_base::
wr_begin()
{
    wr_.autofrag = wr_autofrag_;
    wr_.compress = static_cast<bool>(pmd_);

    // Maintain the write buffer
    if( wr_.compress ||
        role_ == detail::role_type::client)
    {
        if(! wr_.buf || wr_.buf_size != wr_buf_size_)
        {
            wr_.buf_size = wr_buf_size_;
            wr_.buf.reset(new std::uint8_t[wr_.buf_size]);
        }
    }
    else
    {
        wr_.buf_size = wr_buf_size_;
        wr_.buf.reset();
    }
}

template<class DynamicBuffer>
void
stream_base::
write_close(DynamicBuffer& db, close_reason const& cr)
{
    using namespace boost::endian;
    frame_header fh;
    fh.op = opcode::close;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = cr.code == close_code::none ?
        0 : 2 + cr.reason.size();
    fh.mask = role_ == detail::role_type::client;
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

template<class DynamicBuffer>
void
stream_base::
write_ping(
    DynamicBuffer& db, opcode op, ping_data const& data)
{
    frame_header fh;
    fh.op = op;
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

} // detail
} // websocket
} // beast

#endif
