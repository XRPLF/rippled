//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_BASIC_FIELDS_HPP
#define BEAST_HTTP_DETAIL_BASIC_FIELDS_HPP

#include <beast/core/detail/ci_char_traits.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/utility/string_ref.hpp>

namespace beast {
namespace http {

template<class Allocator>
class basic_fields;

namespace detail {

class basic_fields_base
{
public:
    struct value_type
    {
        std::string first;
        std::string second;

        value_type(boost::string_ref const& name_,
                boost::string_ref const& value_)
            : first(name_)
            , second(value_)
        {
        }

        boost::string_ref
        name() const
        {
            return first;
        }

        boost::string_ref
        value() const
        {
            return second;
        }
    };

protected:
    template<class Allocator>
    friend class beast::http::basic_fields;

    struct element
        : boost::intrusive::set_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>>
        , boost::intrusive::list_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>>
    {
        value_type data;

        element(boost::string_ref const& name,
                boost::string_ref const& value)
            : data(name, value)
        {
        }
    };

    struct less : private beast::detail::ci_less
    {
        template<class String>
        bool
        operator()(String const& lhs, element const& rhs) const
        {
            return ci_less::operator()(lhs, rhs.data.first);
        }

        template<class String>
        bool
        operator()(element const& lhs, String const& rhs) const
        {
            return ci_less::operator()(lhs.data.first, rhs);
        }

        bool
        operator()(element const& lhs, element const& rhs) const
        {
            return ci_less::operator()(
                lhs.data.first, rhs.data.first);
        }
    };

    using list_t = boost::intrusive::make_list<element,
        boost::intrusive::constant_time_size<false>>::type;

    using set_t = boost::intrusive::make_multiset<element,
        boost::intrusive::constant_time_size<true>,
            boost::intrusive::compare<less>>::type;

    // data
    set_t set_;
    list_t list_;

    basic_fields_base(set_t&& set, list_t&& list)
        : set_(std::move(set))
        , list_(std::move(list))
    {
    }

public:
    class const_iterator;

    using iterator = const_iterator;

    basic_fields_base() = default;
};

//------------------------------------------------------------------------------

class basic_fields_base::const_iterator
{
    using iter_type = list_t::const_iterator;

    iter_type it_;

    template<class Allocator>
    friend class beast::http::basic_fields;

    friend class basic_fields_base;

    const_iterator(iter_type it)
        : it_(it)
    {
    }

public:
    using value_type =
        typename basic_fields_base::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    const_iterator() = default;
    const_iterator(const_iterator&& other) = default;
    const_iterator(const_iterator const& other) = default;
    const_iterator& operator=(const_iterator&& other) = default;
    const_iterator& operator=(const_iterator const& other) = default;

    bool
    operator==(const_iterator const& other) const
    {
        return it_ == other.it_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return it_->data;
    }

    pointer
    operator->() const
    {
        return &**this;
    }

    const_iterator&
    operator++()
    {
        ++it_;
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    const_iterator&
    operator--()
    {
        --it_;
        return *this;
    }

    const_iterator
    operator--(int)
    {
        auto temp = *this;
        --(*this);
        return temp;
    }
};

} // detail
} // http
} // beast

#endif
