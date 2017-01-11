//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_PMD_EXTENSION_HPP
#define BEAST_WEBSOCKET_DETAIL_PMD_EXTENSION_HPP

#include <beast/core/error.hpp>
#include <beast/core/consuming_buffers.hpp>
#include <beast/core/detail/ci_char_traits.hpp>
#include <beast/zlib/deflate_stream.hpp>
#include <beast/zlib/inflate_stream.hpp>
#include <beast/websocket/option.hpp>
#include <beast/http/rfc7230.hpp>
#include <boost/asio/buffer.hpp>
#include <utility>

namespace beast {
namespace websocket {
namespace detail {

// permessage-deflate offer parameters
//
// "context takeover" means:
// preserve sliding window across messages
//
struct pmd_offer
{
    bool accept;

    // 0 = absent, or 8..15
    int server_max_window_bits;

    // -1 = present, 0 = absent, or 8..15
    int client_max_window_bits;

    // `true` if server_no_context_takeover offered
    bool server_no_context_takeover;

    // `true` if client_no_context_takeover offered
    bool client_no_context_takeover;
};

template<class = void>
int
parse_bits(boost::string_ref const& s)
{
    if(s.size() == 0)
        return -1;
    if(s.size() > 2)
        return -1;
    if(s[0] < '1' || s[0] > '9')
        return -1;
    int i = 0;
    for(auto c : s)
    {
        if(c < '0' || c > '9')
            return -1;
        i = 10 * i + (c - '0');
    }
    return i;
}

// Parse permessage-deflate request fields
//
template<class Fields>
void
pmd_read(pmd_offer& offer, Fields const& fields)
{
    offer.accept = false;
    offer.server_max_window_bits= 0;
    offer.client_max_window_bits = 0;
    offer.server_no_context_takeover = false;
    offer.client_no_context_takeover = false;

    using beast::detail::ci_equal;
    http::ext_list list{
        fields["Sec-WebSocket-Extensions"]};
    for(auto const& ext : list)
    {
        if(ci_equal(ext.first, "permessage-deflate"))
        {
            for(auto const& param : ext.second)
            {
                if(ci_equal(param.first,
                    "server_max_window_bits"))
                {
                    if(offer.server_max_window_bits != 0)
                    {
                        // The negotiation offer contains multiple
                        // extension parameters with the same name.
                        //
                        return; // MUST decline
                    }
                    if(param.second.empty())
                    {
                        // The negotiation offer extension
                        // parameter is missing the value.
                        //
                        return; // MUST decline
                    }
                    offer.server_max_window_bits =
                        parse_bits(param.second);
                    if( offer.server_max_window_bits < 8 ||
                        offer.server_max_window_bits > 15)
                    {
                        // The negotiation offer contains an
                        // extension parameter with an invalid value.
                        //
                        return; // MUST decline
                    }
                }
                else if(ci_equal(param.first,
                    "client_max_window_bits"))
                {
                    if(offer.client_max_window_bits != 0)
                    {
                        // The negotiation offer contains multiple
                        // extension parameters with the same name.
                        //
                        return; // MUST decline
                    }
                    if(! param.second.empty())
                    {
                        offer.client_max_window_bits =
                            parse_bits(param.second);
                        if( offer.client_max_window_bits < 8 ||
                            offer.client_max_window_bits > 15)
                        {
                            // The negotiation offer contains an
                            // extension parameter with an invalid value.
                            //
                            return; // MUST decline
                        }
                    }
                    else
                    {
                        offer.client_max_window_bits = -1;
                    }
                }
                else if(ci_equal(param.first,
                    "server_no_context_takeover"))
                {
                    if(offer.server_no_context_takeover)
                    {
                        // The negotiation offer contains multiple
                        // extension parameters with the same name.
                        //
                        return; // MUST decline
                    }
                    if(! param.second.empty())
                    {
                        // The negotiation offer contains an
                        // extension parameter with an invalid value.
                        //
                        return; // MUST decline
                    }
                    offer.server_no_context_takeover = true;
                }
                else if(ci_equal(param.first,
                    "client_no_context_takeover"))
                {
                    if(offer.client_no_context_takeover)
                    {
                        // The negotiation offer contains multiple
                        // extension parameters with the same name.
                        //
                        return; // MUST decline
                    }
                    if(! param.second.empty())
                    {
                        // The negotiation offer contains an
                        // extension parameter with an invalid value.
                        //
                        return; // MUST decline
                    }
                    offer.client_no_context_takeover = true;
                }
                else
                {
                    // The negotiation offer contains an extension
                    // parameter not defined for use in an offer.
                    //
                    return; // MUST decline
                }
            }
            offer.accept = true;
            return;
        }
    }
}

// Set permessage-deflate fields for a client offer
//
template<class Fields>
void
pmd_write(Fields& fields, pmd_offer const& offer)
{
    std::string s;
    s = "permessage-deflate";
    if(offer.server_max_window_bits != 0)
    {
        if(offer.server_max_window_bits != -1)
        {
            s += "; server_max_window_bits=";
            s += std::to_string(
                offer.server_max_window_bits);
        }
        else
        {
            s += "; server_max_window_bits";
        }
    }
    if(offer.client_max_window_bits != 0)
    {
        if(offer.client_max_window_bits != -1)
        {
            s += "; client_max_window_bits=";
            s += std::to_string(
                offer.server_max_window_bits);
        }
        else
        {
            s += "; client_max_window_bits";
        }
    }
    if(offer.server_no_context_takeover)
    {
        s += "; server_no_context_takeover";
    }
    if(offer.client_no_context_takeover)
    {
        s += "; client_no_context_takeover";
    }
    fields.replace("Sec-WebSocket-Extensions", s);
}

// Negotiate a permessage-deflate client offer
//
template<class Fields>
void
pmd_negotiate(
    Fields& fields,
    pmd_offer& config,
    pmd_offer const& offer,
    permessage_deflate const& o)
{
    if(! (offer.accept && o.server_enable))
    {
        config.accept = false;
        return;
    }
    config.accept = true;

    std::string s = "permessage-deflate";

    config.server_no_context_takeover =
        offer.server_no_context_takeover ||
            o.server_no_context_takeover;
    if(config.server_no_context_takeover)
        s += "; server_no_context_takeover";

    config.client_no_context_takeover =
        o.client_no_context_takeover ||
            offer.client_no_context_takeover;
    if(config.client_no_context_takeover)
        s += "; client_no_context_takeover";

    if(offer.server_max_window_bits != 0)
        config.server_max_window_bits = std::min(
            offer.server_max_window_bits,
                o.server_max_window_bits);
    else
        config.server_max_window_bits =
            o.server_max_window_bits;
    if(config.server_max_window_bits < 15)
    {
        // ZLib's deflateInit silently treats 8 as
        // 9 due to a bug, so prevent 8 from being used.
        //
        if(config.server_max_window_bits < 9)
            config.server_max_window_bits = 9;

        s += "; server_max_window_bits=";
        s += std::to_string(
            config.server_max_window_bits);
    }

    switch(offer.client_max_window_bits)
    {
    case -1:
        // extension parameter is present with no value
        config.client_max_window_bits =
            o.client_max_window_bits;
        if(config.client_max_window_bits < 15)
        {
            s += "client_max_window_bits=";
            s += std::to_string(
                config.client_max_window_bits);
        }
        break;

    case 0:
        /*  extension parameter is absent.

            If a received extension negotiation offer doesn't have the
            "client_max_window_bits" extension parameter, the corresponding
            extension negotiation response to the offer MUST NOT include the
            "client_max_window_bits" extension parameter.
        */
        if(o.client_max_window_bits == 15)
            config.client_max_window_bits = 15;
        else
            config.accept = false;
        break;

    default:
        // extension parameter has value in [8..15]
        if(o.client_max_window_bits <
           offer.client_max_window_bits)
        {
            // Use server's lower configured limit
            config.client_max_window_bits =
                o.client_max_window_bits;
            s += "client_max_window_bits=";
            s += std::to_string(
                config.client_max_window_bits);
        }
        else
        {
            config.client_max_window_bits =
                offer.client_max_window_bits;
        }
        break;
    }
    if(config.accept)
        fields.replace("Sec-WebSocket-Extensions", s);
}

// Normalize the server's response
//
inline
void
pmd_normalize(pmd_offer& offer)
{
    if(offer.accept)
    {
        if( offer.server_max_window_bits == 0)
            offer.server_max_window_bits = 15;

        if( offer.client_max_window_bits ==  0 ||
            offer.client_max_window_bits == -1)
            offer.client_max_window_bits = 15;
    }
}

//--------------------------------------------------------------------

// Decompress into a DynamicBuffer
//
template<class InflateStream, class DynamicBuffer>
void
inflate(
    InflateStream& zi,
    DynamicBuffer& dynabuf,
    boost::asio::const_buffer const& in,
    error_code& ec)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    zlib::z_params zs;
    zs.avail_in = buffer_size(in);
    zs.next_in = buffer_cast<void const*>(in);
    for(;;)
    {
        auto const bs = dynabuf.prepare(
            read_size_helper(dynabuf, 65536));
        auto const out = *bs.begin();
        zs.avail_out = buffer_size(out);
        zs.next_out = buffer_cast<void*>(out);
        zi.write(zs, zlib::Flush::sync, ec);
        dynabuf.commit(zs.total_out);
        zs.total_out = 0;
        if( ec == zlib::error::need_buffers ||
            ec == zlib::error::end_of_stream)
        {
            ec = {};
            break;
        }
        if(ec)
            return;
    }
}

// Compress a buffer sequence
// Returns: `true` if more calls are needed
//
template<class DeflateStream, class ConstBufferSequence>
bool
deflate(
    DeflateStream& zo,
    boost::asio::mutable_buffer& out,
    consuming_buffers<ConstBufferSequence>& cb,
    bool fin,
    error_code& ec)
{
    using boost::asio::buffer;
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    zlib::z_params zs;
    zs.avail_out = buffer_size(out);
    zs.next_out = buffer_cast<void*>(out);
    if(cb.begin() != cb.end())
    {
        for(auto const& in : cb)
        {
            zs.avail_in = buffer_size(in);
            zs.next_in = buffer_cast<void const*>(in);
            zo.write(zs, zlib::Flush::block, ec);
            if(ec == zlib::error::need_buffers)
            {
                ec = {};
                break;
            }
            if(ec)
                return false;
            BOOST_ASSERT(zs.avail_in == 0);
        }
        cb.consume(zs.total_in);
    }
    else
    {
        zs.avail_in = 0;
        zs.next_in = nullptr;
        zo.write(zs, zlib::Flush::block, ec);
        if(ec == zlib::error::need_buffers)
            ec = {};
        if(ec)
            return false;
    }

    if(fin &&
       buffer_size(cb) == 0 &&
       zs.avail_out >= 6)
    {
        zo.write(zs, zlib::Flush::full, ec);
        BOOST_ASSERT(! ec);
        // remove flush marker
        zs.total_out -= 4;
        out = buffer(
            buffer_cast<void*>(out), zs.total_out);
        return false;
    }

    out = buffer(
        buffer_cast<void*>(out), zs.total_out);
    return true;
}

} // detail
} // websocket
} // beast

#endif
