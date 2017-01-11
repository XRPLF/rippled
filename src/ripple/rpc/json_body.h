//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_JSON_BODY_H
#define RIPPLE_RPC_JSON_BODY_H

#include <ripple/json/json_value.h>
#include <beast/core/streambuf.hpp>
#include <beast/http/message.hpp>

namespace ripple {

/// Body that holds JSON
struct json_body
{
    using value_type = Json::Value;

    class writer
    {
        beast::streambuf sb_;

    public:
        template<bool isRequest, class Fields>
        explicit
        writer(beast::http::message<
            isRequest, json_body, Fields> const& m) noexcept
        {
            stream(m.body,
                [&](void const* data, std::size_t n)
                {
                    sb_.commit(boost::asio::buffer_copy(
                        sb_.prepare(n), boost::asio::buffer(data, n)));
                });
        }

        void
        init(beast::error_code&) noexcept
        {
        }

        std::uint64_t
        content_length() const noexcept
        {
            return sb_.size();
        }

        template<class Writef>
        boost::tribool
        write(beast::http::resume_context&&,
            beast::error_code&, Writef&& wf) noexcept
        {
            wf(sb_.data());
            return true;
        }
    };
};

} // ripple

#endif
