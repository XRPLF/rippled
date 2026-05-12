#pragma once

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/range/algorithm/equal.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/utility/string_ref.hpp>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

namespace beast::rfc2616 {

namespace detail {

struct CiEqualPred
{
    explicit CiEqualPred() = default;

    bool
    operator()(char c1, char c2)
    {
        // VFALCO TODO Use a table lookup here
        return std::tolower(static_cast<unsigned char>(c1)) ==
            std::tolower(static_cast<unsigned char>(c2));
    }
};

/** Returns `true` if `c` is linear white space.

    This excludes the CRLF sequence allowed for line continuations.
*/
inline bool
isLws(char c)
{
    return c == ' ' || c == '\t';
}

/** Returns `true` if `c` is any whitespace character. */
inline bool
isWhite(char c)
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
        default:
            return false;
    };
}

template <class FwdIter>
FwdIter
trimRight(FwdIter first, FwdIter last)
{
    if (first == last)
        return last;
    do
    {
        --last;
        if (!isWhite(*last))
            return ++last;
    } while (last != first);
    return first;
}

template <class String>
String
trimRight(String const& s)
{
    using std::begin;
    using std::end;
    auto first(begin(s));
    auto last(end(s));
    last = trimRight(first, last);
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
    class Result = std::vector<std::basic_string<typename std::iterator_traits<FwdIt>::value_type>>,
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
            e = trimRight(e);
            if (!e.empty())
            {
                result.emplace_back(std::move(e));
                e.clear();
            }
            ++iter;
        }
        else if (isLws(*iter))
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
        e = trimRight(e);
        if (!e.empty())
            result.emplace_back(std::move(e));
    }
    return result;
}

template <
    class FwdIt,
    class Result = std::vector<std::basic_string<typename std::iterator_traits<FwdIt>::value_type>>>
Result
splitCommas(FwdIt first, FwdIt last)
{
    return split(first, last, ',');
}

template <class Result = std::vector<std::string>>
Result
splitCommas(boost::beast::string_view const& s)
{
    return splitCommas(s.begin(), s.end());
}

//------------------------------------------------------------------------------

/** Iterates through a comma separated list.

    Meets the requirements of ForwardIterator.

    List defined in rfc2616 2.1.

    @note Values returned may contain backslash escapes.
*/
class ListIterator
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
    using iterator_category = std::forward_iterator_tag;

    ListIterator(iter_type begin, iter_type end) : it_(begin), end_(end)
    {
        if (it_ != end_)
            increment();
    }

    bool
    operator==(ListIterator const& other) const
    {
        return other.it_ == it_ && other.end_ == end_ && other.value_.size() == value_.size();
    }

    bool
    operator!=(ListIterator const& other) const
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

    ListIterator&
    operator++()
    {
        increment();
        return *this;
    }

    ListIterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

private:
    template <class = void>
    void
    increment();
};

template <class>
void
ListIterator::increment()
{
    using namespace detail;
    value_.clear();
    while (it_ != end_)
    {
        if (*it_ == '"')
        {
            // quoted-string
            ++it_;
            if (it_ == end_)
                return;
            if (*it_ != '"')
            {
                auto start = it_;
                for (;;)
                {
                    ++it_;
                    if (it_ == end_)
                    {
                        value_ = boost::string_ref(&*start, std::distance(start, it_));
                        return;
                    }
                    if (*it_ == '"')
                    {
                        value_ = boost::string_ref(&*start, std::distance(start, it_));
                        ++it_;
                        return;
                    }
                }
            }
            ++it_;
        }
        else if (*it_ == ',')
        {
            it_++;
            continue;
        }
        else if (isLws(*it_))
        {
            ++it_;
            continue;
        }
        else
        {
            auto start = it_;
            for (;;)
            {
                ++it_;
                if (it_ == end_ || *it_ == ',' || isLws(*it_))
                {
                    value_ = boost::string_ref(&*start, std::distance(start, it_));
                    return;
                }
            }
        }
    }
}
/** Returns true if two strings are equal.

    A case-insensitive comparison is used.
*/
inline bool
ciEqual(boost::string_ref s1, boost::string_ref s2)
{
    return boost::range::equal(s1, s2, detail::CiEqualPred{});
}

/** Returns a range representing the list. */
inline boost::iterator_range<ListIterator>
makeList(boost::string_ref const& field)
{
    return boost::iterator_range<ListIterator>{
        ListIterator{field.begin(), field.end()}, ListIterator{field.end(), field.end()}};
}

/** Returns true if the specified token exists in the list.

    A case-insensitive comparison is used.
*/
template <class = void>
bool
tokenInList(boost::string_ref const& value, boost::string_ref const& token)
{
    for (auto const& item : makeList(value))
    {
        if (ciEqual(item, token))
            return true;
    }
    return false;
}

template <bool IsRequest, class Body, class Fields>
bool
isKeepAlive(boost::beast::http::message<IsRequest, Body, Fields> const& m)
{
    if (m.version() <= 10)
    {
        return boost::beast::http::token_list{m[boost::beast::http::field::connection]}.exists(
            "keep-alive");
    }
    return !boost::beast::http::token_list{m[boost::beast::http::field::connection]}.exists(
        "close");
}

}  // namespace beast::rfc2616
