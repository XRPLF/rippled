//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_RFC7230_IPP
#define BEAST_HTTP_IMPL_RFC7230_IPP

#include <beast/core/detail/ci_char_traits.hpp>
#include <beast/http/detail/rfc7230.hpp>
#include <iterator>

namespace beast {
namespace http {

class param_list::const_iterator
{
    using iter_type = boost::string_ref::const_iterator;

    std::string s_;
    detail::param_iter pi_;

public:
    using value_type = param_list::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    const_iterator() = default;

    bool
    operator==(const_iterator const& other) const
    {
        return
            other.pi_.it == pi_.it &&
            other.pi_.end == pi_.end &&
            other.pi_.begin == pi_.begin;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return pi_.v;
    }

    pointer
    operator->() const
    {
        return &*(*this);
    }

    const_iterator&
    operator++()
    {
        increment();
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

private:
    friend class param_list;

    const_iterator(iter_type begin, iter_type end)
    {
        pi_.it = begin;
        pi_.begin = begin;
        pi_.end = end;
        increment();
    }

    template<class = void>
    static
    std::string
    unquote(boost::string_ref const& sr);

    template<class = void>
    void
    increment();
};

inline
auto
param_list::
begin() const ->
    const_iterator
{
    return const_iterator{s_.begin(), s_.end()};
}

inline
auto
param_list::
end() const ->
    const_iterator
{
    return const_iterator{s_.end(), s_.end()};
}

inline
auto
param_list::
cbegin() const ->
    const_iterator
{
    return const_iterator{s_.begin(), s_.end()};
}

inline
auto
param_list::
cend() const ->
    const_iterator
{
    return const_iterator{s_.end(), s_.end()};
}

template<class>
std::string
param_list::const_iterator::
unquote(boost::string_ref const& sr)
{
    std::string s;
    s.reserve(sr.size());
    auto it = sr.begin() + 1;
    auto end = sr.end() - 1;
    while(it != end)
    {
        if(*it == '\\')
            ++it;
        s.push_back(*it);
        ++it;
    }
    return s;
}

template<class>
void
param_list::const_iterator::
increment()
{
    s_.clear();
    pi_.increment();
    if(pi_.empty())
    {
        pi_.it = pi_.end;
        pi_.begin = pi_.end;
    }
    else if(pi_.v.second.front() == '"')
    {
        s_ = unquote(pi_.v.second);
        pi_.v.second = boost::string_ref{
            s_.data(), s_.size()};
    }
}

//------------------------------------------------------------------------------

class ext_list::const_iterator
{
    ext_list::value_type v_;
    iter_type it_;
    iter_type begin_;
    iter_type end_;

public:
    using value_type = ext_list::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    const_iterator() = default;

    bool
    operator==(const_iterator const& other) const
    {
        return
            other.it_ == it_ &&
            other.begin_ == begin_ &&
            other.end_ == end_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return v_;
    }

    pointer
    operator->() const
    {
        return &*(*this);
    }

    const_iterator&
    operator++()
    {
        increment();
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

private:
    friend class ext_list;

    const_iterator(iter_type begin, iter_type end)
    {
        it_ = begin;
        begin_ = begin;
        end_ = end;
        increment();
    }

    template<class = void>
    void
    increment();
};

inline
auto
ext_list::
begin() const ->
    const_iterator
{
    return const_iterator{s_.begin(), s_.end()};
}

inline
auto
ext_list::
end() const ->
    const_iterator
{
    return const_iterator{s_.end(), s_.end()};
}

inline
auto
ext_list::
cbegin() const ->
    const_iterator
{
    return const_iterator{s_.begin(), s_.end()};
}

inline
auto
ext_list::
cend() const ->
    const_iterator
{
    return const_iterator{s_.end(), s_.end()};
}

template<class T>
auto
ext_list::
find(T const& s) ->
    const_iterator
{
    return std::find_if(begin(), end(),
        [&s](value_type const& v)
        {
            return beast::detail::ci_equal(s, v.first);
        });
}

template<class T>
bool
ext_list::
exists(T const& s)
{
    return find(s) != end();
}

template<class>
void
ext_list::const_iterator::
increment()
{
    /*
        ext-list    = *( "," OWS ) ext *( OWS "," [ OWS ext ] )
        ext         = token param-list
        param-list  = *( OWS ";" OWS param )
        param       = token OWS "=" OWS ( token / quoted-string )

        chunked;a=b;i=j,gzip;windowBits=12
        x,y
        ,,,,,chameleon
    */
    auto const err =
        [&]
        {
            it_ = end_;
            begin_ = end_;
        };
    auto need_comma = it_ != begin_;
    v_.first = {};
    begin_ = it_;
    for(;;)
    {
        detail::skip_ows(it_, end_);
        if(it_ == end_)
            return err();
        auto const c = *it_;
        if(detail::is_tchar(c))
        {
            if(need_comma)
                return err();
            auto const p0 = it_;
            for(;;)
            {
                ++it_;
                if(it_ == end_)
                    break;
                if(! detail::is_tchar(*it_))
                    break;
            }
            v_.first = boost::string_ref{&*p0,
                static_cast<std::size_t>(it_ - p0)};
            detail::param_iter pi;
            pi.it = it_;
            pi.begin = it_;
            pi.end = end_;
            for(;;)
            {
                pi.increment();
                if(pi.empty())
                    break;
            }
            v_.second = param_list{boost::string_ref{&*it_,
                static_cast<std::size_t>(pi.it - it_)}};
            it_ = pi.it;
            return;
        }
        if(c != ',')
            return err();
        need_comma = false;
        ++it_;
    }
}

//------------------------------------------------------------------------------

class token_list::const_iterator
{
    token_list::value_type v_;
    iter_type it_;
    iter_type begin_;
    iter_type end_;

public:
    using value_type = token_list::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    const_iterator() = default;

    bool
    operator==(const_iterator const& other) const
    {
        return
            other.it_ == it_ &&
            other.begin_ == begin_ &&
            other.end_ == end_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return v_;
    }

    pointer
    operator->() const
    {
        return &*(*this);
    }

    const_iterator&
    operator++()
    {
        increment();
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

private:
    friend class token_list;

    const_iterator(iter_type begin, iter_type end)
    {
        it_ = begin;
        begin_ = begin;
        end_ = end;
        increment();
    }

    template<class = void>
    void
    increment();
};

inline
auto
token_list::
begin() const ->
    const_iterator
{
    return const_iterator{s_.begin(), s_.end()};
}

inline
auto
token_list::
end() const ->
    const_iterator
{
    return const_iterator{s_.end(), s_.end()};
}

inline
auto
token_list::
cbegin() const ->
    const_iterator
{
    return const_iterator{s_.begin(), s_.end()};
}

inline
auto
token_list::
cend() const ->
    const_iterator
{
    return const_iterator{s_.end(), s_.end()};
}

template<class>
void
token_list::const_iterator::
increment()
{
    /*
        token-list  = *( "," OWS ) token *( OWS "," [ OWS ext ] )
    */
    auto const err =
        [&]
        {
            it_ = end_;
            begin_ = end_;
        };
    auto need_comma = it_ != begin_;
    v_ = {};
    begin_ = it_;
    for(;;)
    {
        detail::skip_ows(it_, end_);
        if(it_ == end_)
            return err();
        auto const c = *it_;
        if(detail::is_tchar(c))
        {
            if(need_comma)
                return err();
            auto const p0 = it_;
            for(;;)
            {
                ++it_;
                if(it_ == end_)
                    break;
                if(! detail::is_tchar(*it_))
                    break;
            }
            v_ = boost::string_ref{&*p0,
                static_cast<std::size_t>(it_ - p0)};
            return;
        }
        if(c != ',')
            return err();
        need_comma = false;
        ++it_;
    }
}

template<class T>
bool
token_list::
exists(T const& s)
{
    return std::find_if(begin(), end(),
        [&s](value_type const& v)
        {
            return beast::detail::ci_equal(s, v);
        }
    ) != end();
}

} // http
} // beast

#endif

