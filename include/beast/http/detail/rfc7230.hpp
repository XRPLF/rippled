//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_RFC7230_HPP
#define BEAST_HTTP_DETAIL_RFC7230_HPP

#include <boost/utility/string_ref.hpp>
#include <array>
#include <iterator>
#include <utility>

namespace beast {
namespace http {
namespace detail {

inline
bool
is_tchar(char c)
{
    /*
        tchar = "!" | "#" | "$" | "%" | "&" |
                "'" | "*" | "+" | "-" | "." |
                "^" | "_" | "`" | "|" | "~" |
                DIGIT | ALPHA
    */
    static std::array<bool, 256> constexpr tab = {{
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, // 0
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, // 16
        0, 1, 0, 1,  1, 1, 1, 1,  0, 0, 1, 1,  0, 1, 1, 0, // 32
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 0, 0,  0, 0, 0, 0, // 48
        0, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 64
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 0,  0, 0, 1, 1, // 80
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 96
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 0,  1, 0, 1, 0, // 112
    }};
    return tab[static_cast<std::uint8_t>(c)];
}

inline
bool
is_qdchar(char c)
{
    /*
        qdtext = HTAB / SP / "!" / %x23-5B ; '#'-'[' / %x5D-7E ; ']'-'~' / obs-text
    */
    static std::array<bool, 256> constexpr tab = {{
        0, 0, 0, 0,  0, 0, 0, 0,  0, 1, 0, 0,  0, 0, 0, 0, // 0
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, // 16
        1, 1, 0, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 32
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 48
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 64
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  0, 1, 1, 1, // 80
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 96
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 0, // 112
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 128
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 144
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 160
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 176
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 192
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 208
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 224
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1  // 240
    }};
    return tab[static_cast<std::uint8_t>(c)];
}

inline
bool
is_qpchar(char c)
{
    /*
        quoted-pair = "\" ( HTAB / SP / VCHAR / obs-text )
        obs-text = %x80-FF
    */
    static std::array<bool, 256> constexpr tab = {{
        0, 0, 0, 0,  0, 0, 0, 0,  0, 1, 0, 0,  0, 0, 0, 0, // 0
        0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, // 16
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 32
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 48
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 64
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 80
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 96
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 0, // 112
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 128
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 144
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 160
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 176
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 192
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 208
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1, // 224
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1  // 240
    }};
    return tab[static_cast<std::uint8_t>(c)];
}

template<class FwdIt>
void
skip_ows(FwdIt& it, FwdIt const& end)
{
    while(it != end)
    {
        auto const c = *it;
        if(c != ' ' && c != '\t')
            break;
        ++it;
    }
}

inline
boost::string_ref
trim(boost::string_ref const& s)
{
    auto first = s.begin();
    auto last = s.end();
    skip_ows(first, last);
    while(first != last)
    {
        auto const c = *std::prev(last);
        if(c != ' ' && c != '\t')
            break;
        --last;
    }
    if(first == last)
        return {};
    return {&*first,
        static_cast<std::size_t>(last - first)};
}

struct param_iter
{
    using iter_type = boost::string_ref::const_iterator;

    iter_type it;
    iter_type begin;
    iter_type end;
    std::pair<boost::string_ref, boost::string_ref> v;

    bool
    empty() const
    {
        return begin == it;
    }

    template<class = void>
    void
    increment();
};

template<class>
void
param_iter::
increment()
{
/*
    ext-list    = *( "," OWS ) ext *( OWS "," [ OWS ext ] )
    ext         = token param-list
    param-list  = *( OWS ";" OWS param )
    param       = token OWS "=" OWS ( token / quoted-string )
            
    quoted-string = DQUOTE *( qdtext / quoted-pair ) DQUOTE
    qdtext = HTAB / SP / "!" / %x23-5B ; '#'-'[' / %x5D-7E ; ']'-'~' / obs-text
    quoted-pair = "\" ( HTAB / SP / VCHAR / obs-text )
    obs-text = %x80-FF

    Example:
        chunked;a=b;i=j,gzip;windowBits=12
        x,y
*/
    auto const err =
        [&]
        {
            it = begin;
        };
    v.first.clear();
    v.second.clear();
    detail::skip_ows(it, end);
    begin = it;
    if(it == end)
        return err();
    if(*it != ';')
        return err();
    ++it;
    detail::skip_ows(it, end);
    if(it == end)
        return err();
    // param
    if(! detail::is_tchar(*it))
        return err();
    auto const p0 = it;
    for(;;)
    {
        ++it;
        if(it == end)
            return err();
        if(! detail::is_tchar(*it))
            break;
    }
    auto const p1 = it;
    detail::skip_ows(it, end);
    if(it == end)
        return err();
    if(*it != '=')
        return err();
    ++it;
    detail::skip_ows(it, end);
    if(it == end)
        return err();
    if(*it == '"')
    {
        // quoted-string
        auto const p2 = it;
        ++it;
        for(;;)
        {
            if(it == end)
                return err();
            auto c = *it++;
            if(c == '"')
                break;
            if(detail::is_qdchar(c))
                continue;
            if(c != '\\')
                return err();
            if(it == end)
                return err();
            c = *it++;
            if(! detail::is_qpchar(c))
                return err();
        }
        v.first = { &*p0, static_cast<std::size_t>(p1 - p0) };
        v.second = { &*p2, static_cast<std::size_t>(it - p2) };
    }
    else
    {
        // token
        if(! detail::is_tchar(*it))
            return err();
        auto const p2 = it;
        for(;;)
        {
            it++;
            if(it == end)
                break;
            if(! detail::is_tchar(*it))
                break;
        }
        v.first = { &*p0, static_cast<std::size_t>(p1 - p0) };
        v.second = { &*p2, static_cast<std::size_t>(it - p2) };
    }
}

} // detail
} // http
} // beast

#endif

