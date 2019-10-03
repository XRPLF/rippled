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
#include <string>
#include <iterator>
#include <tuple> // for std::tie, remove ASAP
#include <utility>
#include <vector>

namespace beast {
namespace rfc2616 {

namespace detail {

/*  Routines for performing RFC2616 compliance.
    RFC2616:
        Hypertext Transfer Protocol -- HTTP/1.1
        http://www.w3.org/Protocols/rfc2616/rfc2616
*/

struct ci_equal_pred
{
    explicit ci_equal_pred() = default;

    bool operator()(char c1, char c2)
    {
        // VFALCO TODO Use a table lookup here
        return std::tolower(static_cast<unsigned char>(c1)) ==
               std::tolower(static_cast<unsigned char>(c2));
    }
};

} // detail

/** Returns `true` if `c` is linear white space.

    This excludes the CRLF sequence allowed for line continuations.
*/
inline
bool
is_lws(char c)
{
    return c == ' ' || c == '\t';
}

/** Returns `true` if `c` is any whitespace character. */
inline
bool
is_white(char c)
{
    switch (c)
    {
    case ' ':  case '\f': case '\n':
    case '\r': case '\t': case '\v':
        return true;
    };
    return false;
}

/** Returns `true` if `c` is a control character. */
inline
bool
is_control(char c)
{
    return c <= 31 || c >= 127;
}

/** Returns `true` if `c` is a separator. */
inline
bool
is_separator(char c)
{
    // VFALCO Could use a static table
    switch (c)
    {
    case '(': case ')': case '<': case '>':  case '@':
    case ',': case ';': case ':': case '\\': case '"':
    case '{': case '}': case ' ': case '\t':
        return true;
    };
    return false;
}

/** Returns `true` if `c` is a character. */
inline
bool
is_char(char c)
{
#ifdef __CHAR_UNSIGNED__  /* -funsigned-char */
    return c >= 0 && c <= 127;
#else
    return c >= 0;
#endif
}

template <class FwdIter>
FwdIter
trim_left (FwdIter first, FwdIter last)
{
    return std::find_if_not (first, last,
        is_white);
}

template <class FwdIter>
FwdIter
trim_right (FwdIter first, FwdIter last)
{
    if (first == last)
        return last;
    do
    {
        --last;
        if (! is_white (*last))
            return ++last;
    }
    while (last != first);
    return first;
}

template <class CharT, class Traits, class Allocator>
void
trim_right_in_place (std::basic_string <
    CharT, Traits, Allocator>& s)
{
    s.resize (std::distance (s.begin(),
        trim_right (s.begin(), s.end())));
}

template <class FwdIter>
std::pair <FwdIter, FwdIter>
trim (FwdIter first, FwdIter last)
{
    first = trim_left (first, last);
    last = trim_right (first, last);
    return std::make_pair (first, last);
}

template <class String>
String
trim (String const& s)
{
    using std::begin;
    using std::end;
    auto first = begin(s);
    auto last = end(s);
    std::tie (first, last) = trim (first, last);
    return { first, last };
}

template <class String>
String
trim_right (String const& s)
{
    using std::begin;
    using std::end;
    auto first (begin(s));
    auto last (end(s));
    last = trim_right (first, last);
    return { first, last };
}

inline
std::string
trim (std::string const& s)
{
    return trim <std::string> (s);
}

/** Parse a character sequence of values separated by commas.
    Double quotes and escape sequences will be converted.  Excess white
    space, commas, double quotes, and empty elements are not copied.
    Format:
       #(token|quoted-string)
    Reference:
        http://www.w3.org/Protocols/rfc2616/rfc2616-sec2.html#sec2
*/
template <class FwdIt,
    class Result = std::vector<
        std::basic_string<typename
            std::iterator_traits<FwdIt>::value_type>>,
                class Char>
Result
split(FwdIt first, FwdIt last, Char delim)
{
    Result result;
    using string = typename Result::value_type;
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
                        e.append (1, *iter++);
                }
                else
                {
                    // qdtext
                    e.append (1, *iter++);
                }
            }
            if (! e.empty())
            {
                result.emplace_back(std::move(e));
                e.clear();
            }
        }
        else if (*iter == delim)
        {
            e = trim_right (e);
            if (! e.empty())
            {
                result.emplace_back(std::move(e));
                e.clear();
            }
            ++iter;
        }
        else if (is_lws (*iter))
        {
            ++iter;
        }
        else
        {
            e.append (1, *iter++);
        }
    }

    if (! e.empty())
    {
        e = trim_right (e);
        if (! e.empty())
            result.emplace_back(std::move(e));
    }
    return result;
}

template <class FwdIt,
    class Result = std::vector<
        std::basic_string<typename std::iterator_traits<
            FwdIt>::value_type>>>
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

//------------------------------------------------------------------------------

/** Iterates through a comma separated list.

    Meets the requirements of ForwardIterator.

    List defined in rfc2616 2.1.

    @note Values returned may contain backslash escapes.
*/
class list_iterator
{
    using iter_type = boost::string_ref::const_iterator;

    iter_type it_;
    iter_type end_;
    boost::string_ref value_;

public:
    using value_type = boost::string_ref;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::forward_iterator_tag;

    list_iterator(iter_type begin, iter_type end)
        : it_(begin)
        , end_(end)
    {
        if(it_ != end_)
            increment();
    }

    bool
    operator==(list_iterator const& other) const
    {
        return other.it_ == it_ && other.end_ == end_
            && other.value_.size() == value_.size();
    }

    bool
    operator!=(list_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return value_;
    }

    pointer
    operator->() const
    {
        return &*(*this);
    }

    list_iterator&
    operator++()
    {
        increment();
        return *this;
    }

    list_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

private:
    template<class = void>
    void
    increment();
};

template<class>
void
list_iterator::increment()
{
    value_.clear();
    while(it_ != end_)
    {
        if(*it_ == '"')
        {
            // quoted-string
            ++it_;
            if(it_ == end_)
                return;
            if(*it_ != '"')
            {
                auto start = it_;
                for(;;)
                {
                    ++it_;
                    if(it_ == end_)
                    {
                        value_ = boost::string_ref(
                            &*start, std::distance(start, it_));
                        return;
                    }
                    if(*it_ == '"')
                    {
                        value_ = boost::string_ref(
                            &*start, std::distance(start, it_));
                        ++it_;
                        return;
                    }
                }
            }
            ++it_;
        }
        else if(*it_ == ',')
        {
            it_++;
            continue;
        }
        else if(is_lws(*it_))
        {
            ++it_;
            continue;
        }
        else
        {
            auto start = it_;
            for(;;)
            {
                ++it_;
                if(it_ == end_ ||
                    *it_ == ',' ||
                        is_lws(*it_))
                {
                    value_ = boost::string_ref(
                        &*start, std::distance(start, it_));
                    return;
                }
            }
        }
    }
}

/** Returns true if two strings are equal.

    A case-insensitive comparison is used.
*/
inline
bool
ci_equal(boost::string_ref s1, boost::string_ref s2)
{
    return boost::range::equal(s1, s2,
        detail::ci_equal_pred{});
}

/** Returns a range representing the list. */
inline
boost::iterator_range<list_iterator>
make_list(boost::string_ref const& field)
{
    return boost::iterator_range<list_iterator>{
        list_iterator{field.begin(), field.end()},
            list_iterator{field.end(), field.end()}};
}

/** Returns true if the specified token exists in the list.

    A case-insensitive comparison is used.
*/
template<class = void>
bool
token_in_list(boost::string_ref const& value,
    boost::string_ref const& token)
{
    for(auto const& item : make_list(value))
        if(ci_equal(item, token))
            return true;
    return false;
}

template<bool isRequest, class Body, class Fields>
bool
is_keep_alive(boost::beast::http::message<isRequest, Body, Fields> const& m)
{
    if(m.version() <= 10)
        return boost::beast::http::token_list{
            m[boost::beast::http::field::connection]}.exists("keep-alive");
    return ! boost::beast::http::token_list{
        m[boost::beast::http::field::connection]}.exists("close");
}

} // rfc2616
} // beast

#endif
