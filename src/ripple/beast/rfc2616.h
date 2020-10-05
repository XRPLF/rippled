//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_RFC2616_HPP
#define BEAST_RFC2616_HPP

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/range/algorithm/equal.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/utility/string_ref.hpp>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace beast {
namespace rfc2616 {

namespace detail {

/** Returns `true` if `c` is linear white space.

    This excludes the CRLF sequence allowed for line continuations.
*/
inline bool
is_lws(char c)
{
    return c == ' ' || c == '\t';
}

/** Returns `true` if `c` is any whitespace character. */
inline bool
is_white(char c)
{
    switch (c)
    {
        case ' ':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
            return true;
    };
    return false;
}

template <class FwdIter>
FwdIter
trim_right(FwdIter first, FwdIter last)
{
    if (first == last)
        return last;
    do
    {
        --last;
        if (!is_white(*last))
            return ++last;
    } while (last != first);
    return first;
}

template <class String>
String
trim_right(String const& s)
{
    using std::begin;
    using std::end;
    auto first(begin(s));
    auto last(end(s));
    last = trim_right(first, last);
    return {first, last};
}

}  // namespace detail

/** Parse a character sequence of values separated by commas.
    Double quotes and escape sequences will be converted.  Excess white
    space, commas, double quotes, and empty elements are not copied.
    Format:
       #(token|quoted-string)
    Reference:
        http://www.w3.org/Protocols/rfc2616/rfc2616-sec2.html#sec2
*/
template <
    class FwdIt,
    class Result = std::vector<
        std::basic_string<typename std::iterator_traits<FwdIt>::value_type>>,
    class Char>
Result
split(FwdIt first, FwdIt last, Char delim)
{
    using namespace detail;
    using string = typename Result::value_type;

    Result result;

    FwdIt iter = first;
    string e;
    while (iter != last)
    {
        if (*iter == '"')
        {
            // quoted-string
            ++iter;
            while (iter != last)
            {
                if (*iter == '"')
                {
                    ++iter;
                    break;
                }

                if (*iter == '\\')
                {
                    // quoted-pair
                    ++iter;
                    if (iter != last)
                        e.append(1, *iter++);
                }
                else
                {
                    // qdtext
                    e.append(1, *iter++);
                }
            }
            if (!e.empty())
            {
                result.emplace_back(std::move(e));
                e.clear();
            }
        }
        else if (*iter == delim)
        {
            e = trim_right(e);
            if (!e.empty())
            {
                result.emplace_back(std::move(e));
                e.clear();
            }
            ++iter;
        }
        else if (is_lws(*iter))
        {
            ++iter;
        }
        else
        {
            e.append(1, *iter++);
        }
    }

    if (!e.empty())
    {
        e = trim_right(e);
        if (!e.empty())
            result.emplace_back(std::move(e));
    }
    return result;
}

template <
    class FwdIt,
    class Result = std::vector<
        std::basic_string<typename std::iterator_traits<FwdIt>::value_type>>>
Result
split_commas(FwdIt first, FwdIt last)
{
    return split(first, last, ',');
}

template <class Result = std::vector<std::string>>
Result
split_commas(boost::beast::string_view const& s)
{
    return split_commas(s.begin(), s.end());
}

template <bool isRequest, class Body, class Fields>
bool
is_keep_alive(boost::beast::http::message<isRequest, Body, Fields> const& m)
{
    if (m.version() <= 10)
        return boost::beast::http::token_list{
            m[boost::beast::http::field::connection]}
            .exists("keep-alive");
    return !boost::beast::http::token_list{
        m[boost::beast::http::field::connection]}
                .exists("close");
}

}  // namespace rfc2616
}  // namespace beast

#endif
