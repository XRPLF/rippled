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

#ifndef RIPPLE_OVERLAY_BASIC_MESSAGE_H_INCLUDED
#define RIPPLE_OVERLAY_BASIC_MESSAGE_H_INCLUDED

#include <boost/system/error_code.hpp>
#include <beast/http/message_parser.h>
#include <beast/http/method.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <map>
#include <string>

namespace ripple {

class basic_message
{
public:
    typedef boost::system::error_code error_code;

private:
    struct ci_less
    {
        static bool const is_transparent = true;

        template <class String>
        bool
        operator() (String const& lhs, String const& rhs) const
        {
            typedef typename String::value_type char_type;
            return std::lexicographical_compare (std::begin(lhs), std::end(lhs),
                std::begin(rhs), std::end(rhs),
                [] (char_type lhs, char_type rhs)
                {
                    return std::tolower(lhs) < std::tolower(rhs);
                }
            );
        }
    };

    // request
    beast::http::method_t method_;
    std::string url_;

    // response
    std::string reason_;

    // message
    std::pair<int, int> version_;
    std::map <std::string, std::string, ci_less> headers_;

public:
    class parser : public beast::http::message_parser
    {
    private:
        basic_message& message_;

    public:
        parser (basic_message& message, bool request)
            : message_parser (request)
            , message_ (message)
        {
        }

    private:
        error_code
        on_request (beast::http::method_t m, int http_major,
            int http_minor, std::string const& url)
        {
            message_.method (m);
            message_.version (http_major, http_minor);
            message_.url (url);
            return error_code();
        }

        error_code
        on_field (std::string const& field, std::string const& value)
        {
            message_.append_header (field, value);
            return error_code();
        }
    };

    basic_message()
        : method_ (beast::http::method_t::http_get)
        , version_ (1, 1)
    {
    }

    // Request

    void
    method (beast::http::method_t http_method)
    {
        method_ = http_method;
    }

    beast::http::method_t
    method() const
    {
        return method_;
    }

    void
    url (std::string const& s)
    {
        url_ = s;
    }

    std::string const&
    url() const
    {
        return url_;
    }

    // Response

    // Message

    void
    version (int major, int minor)
    {
        version_ = std::make_pair (major, minor);
    }

    std::pair<int, int>
    version() const
    {
        return version_;
    }

    void
    append_header (std::string const& field, std::string const& value)
    {
        auto result (headers_.emplace (field, value));
        if (! result.second)
        {
            // If field already exists, append comma
            // separated value as per RFC2616 section 4.2
            auto& current_value (result.first->second);
            current_value.reserve (current_value.size() + 1 + value.size());
            current_value.append (1, ',');
            current_value.append (value);
        }
    }
};

template <class StreamBuf>
void
write (StreamBuf& stream, basic_message const& m)
{
}

} // ripple

#endif
