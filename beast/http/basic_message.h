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

#ifndef BEAST_HTTP_MESSAGE_H_INCLUDED
#define BEAST_HTTP_MESSAGE_H_INCLUDED

#include <beast/http/method.h>
#include <beast/http/parser.h>
#include <beast/utility/ci_char_traits.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace beast {
namespace http {

class basic_message
{
public:
    class parser;

    typedef boost::system::error_code error_code;

private:
    class element
        : public boost::intrusive::set_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>
            >
        , public boost::intrusive::list_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>
            >
    {
    public:
        element (std::string const& f, std::string const& v)
            : field (f)
            , value (v)
        {
        }

        std::string field;
        std::string value;
    };

    struct less : private beast::ci_less
    {
        template <class String>
        bool
        operator() (String const& lhs, element const& rhs) const
        {
            return beast::ci_less::operator() (lhs, rhs.field);
        }

        template <class String>
        bool
        operator() (element const& lhs, String const& rhs) const
        {
            return beast::ci_less::operator() (lhs.field, rhs);
        }
    };

    class headers_t : private less
    {
    private:
        typedef boost::intrusive::make_list <element,
            boost::intrusive::constant_time_size <false>
                >::type list_t;

        typedef boost::intrusive::make_set <element,
            boost::intrusive::constant_time_size <true>
                >::type set_t;

        list_t list_;
        set_t set_;

    public:
        typedef list_t::const_iterator iterator;

        ~headers_t()
        {
            clear();
        }

        headers_t() = default;

        headers_t (headers_t&& other)
            : list_ (std::move(other.list_))
            , set_ (std::move(other.set_))
        {

        }

        headers_t (headers_t const& other)
        {
            for (auto const& e : other.list_)
                append (e.field, e.value);
        }

        headers_t&
        operator= (headers_t&& other)
        {
            list_ = std::move(other.list_);
            set_ = std::move(other.set_);
            return *this;
        }

        headers_t&
        operator= (headers_t const& other)
        {
            clear();
            for (auto const& e : other.list_)
                append (e.field, e.value);
            return *this;
        }

        void
        clear()
        {
            for (auto iter (list_.begin()); iter != list_.end();)
            {
                element* const p (&*iter);
                ++iter;
                delete p;
            }
        }

        iterator
        begin() const
        {
            return list_.cbegin();
        }

        iterator
        end() const
        {
            return list_.cend();
        }

        iterator
        cbegin() const
        {
            return list_.cbegin();
        }

        iterator
        cend() const
        {
            return list_.cend();
        }

        iterator
        find (std::string const& field) const
        {
            auto const iter (set_.find (field,
                std::cref(static_cast<less const&>(*this))));
            if (iter == set_.end())
                return list_.end();
            return list_.iterator_to (*iter);
        }

        std::string const&
        operator[] (std::string const& field) const
        {
            static std::string none;
            auto const found (find (field));
            if (found == end())
                return none;
            return found->value;
        }

        void
        append (std::string const& field, std::string const& value)
        {
            set_t::insert_commit_data d;
            auto const result (set_.insert_check (field,
                std::cref(static_cast<less const&>(*this)), d));
            if (result.second)
            {
                element* const p (new element (field, value));
                list_.push_back (*p);
                auto const iter (set_.insert_commit (*p, d));
                return;
            }
            // If field already exists, append comma
            // separated value as per RFC2616 section 4.2
            auto& cur (result.first->value);
            cur.reserve (cur.size() + 1 + value.size());
            cur.append (1, ',');
            cur.append (value);
        }
    };

    bool request_;

    // request
    beast::http::method_t method_;
    std::string url_;
    
    // response
    int status_;
    std::string reason_;

    // message
    std::pair<int, int> version_;
    bool keep_alive_;
    bool upgrade_;

public:
    ~basic_message() = default;

    basic_message()
        : request_ (true)
        , method_ (beast::http::method_t::http_get)
        , url_ ("/")
        , status_ (200)
        , version_ (1, 1)
        , keep_alive_ (false)
        , upgrade_ (false)
    {
    }

    basic_message (basic_message&& other)
        : request_ (true)
        , method_ (std::move(other.method_))
        , url_ (std::move(other.url_))
        , status_ (other.status_)
        , reason_ (std::move(other.reason_))
        , version_ (other.version_)
        , keep_alive_ (other.keep_alive_)
        , upgrade_ (other.upgrade_)
        , headers (std::move(other.headers))
    {
    }

    bool
    request() const
    {
        return request_;
    }

    void
    request (bool value)
    {
        request_ = value;
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

    /** Returns `false` if this is not the last message.
        When keep_alive returns `false`:
            * Server roles respond with a "Connection: close" header.
            * Client roles close the connection.
    */
    bool
    keep_alive() const
    {
        return keep_alive_;
    }

    /** Set the keep_alive setting. */
    void
    keep_alive (bool value)
    {
        keep_alive_ = value;
    }

    /** Returns `true` if this is an HTTP Upgrade message.
        @note Upgrade messages have no content body.
    */
    bool
    upgrade() const
    {
        return upgrade_;
    }

    /** Set the upgrade setting. */
    void
    upgrade (bool value)
    {
        upgrade_ = value;
    }

    int
    status() const
    {
        return status_;
    }

    void
    status (int code)
    {
        status_ = code;
    }

    std::string const&
    reason() const
    {
        return reason_;
    }

    void
    reason (std::string const& text)
    {
        reason_ = text;
    }

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

    // Memberspace
    headers_t headers;
};

//------------------------------------------------------------------------------

class basic_message::parser : public beast::http::parser
{
private:
    basic_message& message_;

public:
    parser (basic_message& message, bool request)
        : beast::http::parser (request)
        , message_ (message)
    {
        message_.request(request);
    }

private:
    void
    on_start () override
    {
    }

    bool
    on_request (method_t method, std::string const& url,
        int major, int minor, bool keep_alive, bool upgrade) override
    {
        message_.method (method);
        message_.url (url);
        message_.version (major, minor);
        message_.keep_alive(keep_alive);
        message_.upgrade(upgrade);
        return upgrade ? false : false;
    }

    bool
    on_response (int status, std::string const& text,
        int major, int minor, bool keep_alive, bool upgrade) override
    {
        message_.status (status);
        message_.reason (text);
        message_.version (major, minor);
        message_.keep_alive(keep_alive);
        message_.upgrade(upgrade);
        return upgrade ? false : false;
    }

    void
    on_field (std::string const& field, std::string const& value) override
    {
        message_.headers.append (field, value);
    }

    void
    on_body (void const* data, std::size_t bytes) override
    {
    }

    void
    on_complete() override
    {
    }
};

//------------------------------------------------------------------------------

template <class AsioStreamBuf>
void
xwrite (AsioStreamBuf& stream, std::string const& s)
{
    stream.commit (boost::asio::buffer_copy (
        stream.prepare (s.size()), boost::asio::buffer(s)));
}

template <class AsioStreamBuf>
void
xwrite (AsioStreamBuf& stream, char const* s)
{
    auto const len (::strlen(s));
    stream.commit (boost::asio::buffer_copy (
        stream.prepare (len), boost::asio::buffer (s, len)));
}

template <class AsioStreamBuf>
void
xwrite (AsioStreamBuf& stream, basic_message const& m)
{
    if (m.request())
    {
        xwrite (stream, to_string(m.method()));
        xwrite (stream, " ");
        xwrite (stream, m.url());
        xwrite (stream, " HTTP/");
        xwrite (stream, std::to_string(m.version().first));
        xwrite (stream, ".");
        xwrite (stream, std::to_string(m.version().second));
    }
    else
    {
        xwrite (stream, "HTTP/");
        xwrite (stream, std::to_string(m.version().first));
        xwrite (stream, ".");
        xwrite (stream, std::to_string(m.version().second));
        xwrite (stream, " ");
        xwrite (stream, std::to_string(m.status()));
        xwrite (stream, " ");
        xwrite (stream, m.reason());
    }
    xwrite (stream, "\r\n");
    for (auto const& header : m.headers)
    {
        xwrite (stream, header.field);
        xwrite (stream, ": ");
        xwrite (stream, header.value);
        xwrite (stream, "\r\n");
    }
    xwrite (stream, "\r\n");
}

template <class = void>
std::string
to_string (basic_message const& m)
{
    std::stringstream ss;
    if (m.request())
        ss << to_string(m.method()) << " " << m.url() << " HTTP/" <<
            std::to_string(m.version().first) << "." <<
                std::to_string(m.version().second) << "\r\n";
    else
        ss << "HTTP/" << std::to_string(m.version().first) << "." <<
            std::to_string(m.version().second) << " " <<
                std::to_string(m.status()) << " " << m.reason() << "\r\n";
    for (auto const& header : m.headers)
        ss << header.field << ": " << header.value << "\r\n";
    ss << "\r\n";
    return ss.str();
}

} // http
} // beast

#endif