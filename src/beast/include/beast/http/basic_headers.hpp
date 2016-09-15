//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_BASIC_HEADERS_HPP
#define BEAST_HTTP_BASIC_HEADERS_HPP

#include <beast/core/detail/ci_char_traits.hpp>
#include <beast/core/detail/empty_base_optimization.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/utility/string_ref.hpp>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

template<class Allocator>
class basic_headers;

namespace detail {

class basic_headers_base
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
    friend class beast::http::basic_headers;

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

    using list_t = typename boost::intrusive::make_list<
        element, boost::intrusive::constant_time_size<false>>::type;

    using set_t = typename boost::intrusive::make_multiset<
        element, boost::intrusive::constant_time_size<true>,
            boost::intrusive::compare<less>>::type;

    // data
    set_t set_;
    list_t list_;

    basic_headers_base(set_t&& set, list_t&& list)
        : set_(std::move(set))
        , list_(std::move(list))
    {
    }

public:
    class const_iterator;

    using iterator = const_iterator;

    basic_headers_base() = default;

    /// Returns an iterator to the beginning of the field sequence.
    iterator
    begin() const;

    /// Returns an iterator to the end of the field sequence.
    iterator
    end() const;

    /// Returns an iterator to the beginning of the field sequence.
    iterator
    cbegin() const;

    /// Returns an iterator to the end of the field sequence.
    iterator
    cend() const;
};

//------------------------------------------------------------------------------

class basic_headers_base::const_iterator
{
    using iter_type = list_t::const_iterator;

    iter_type it_;

    template<class Allocator>
    friend class beast::http::basic_headers;

    friend class basic_headers_base;

    const_iterator(iter_type it)
        : it_(it)
    {
    }

public:
    using value_type =
        typename basic_headers_base::value_type;
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

//------------------------------------------------------------------------------

/** A container for storing HTTP headers.

    This container is designed to store the field value pairs that make
    up the headers and trailers in a HTTP message. Objects of this type
    are iterable, which each element holding the field name and field
    value.

    Field names are stored as-is, but comparison are case-insensitive.
    When the container is iterated, the fields are presented in the order
    of insertion. For fields with the same name, the container behaves
    as a std::multiset; there will be a separate value for each occurrence
    of the field name.

    @note Meets the requirements of @b `FieldSequence`.
*/
template<class Allocator>
class basic_headers
#if ! GENERATING_DOCS
    : private beast::detail::empty_base_optimization<
        typename std::allocator_traits<Allocator>::
            template rebind_alloc<
                detail::basic_headers_base::element>>
    , public detail::basic_headers_base
#endif
{
    using alloc_type = typename
        std::allocator_traits<Allocator>::
            template rebind_alloc<
                detail::basic_headers_base::element>;

    using alloc_traits =
        std::allocator_traits<alloc_type>;

    using size_type =
        typename std::allocator_traits<Allocator>::size_type;

    void
    delete_all();

    void
    move_assign(basic_headers&, std::false_type);

    void
    move_assign(basic_headers&, std::true_type);

    void
    copy_assign(basic_headers const&, std::false_type);

    void
    copy_assign(basic_headers const&, std::true_type);

    template<class FieldSequence>
    void
    copy_from(FieldSequence const& fs)
    {
        for(auto const& e : fs)
            insert(e.first, e.second);
    }

public:
    /// The type of allocator used.
    using allocator_type = Allocator;

    /// Default constructor.
    basic_headers() = default;

    /// Destructor
    ~basic_headers();

    /** Construct the headers.

        @param alloc The allocator to use.
    */
    explicit
    basic_headers(Allocator const& alloc);

    /** Move constructor.

        The moved-from object becomes an empty field sequence.

        @param other The object to move from.
    */
    basic_headers(basic_headers&& other);

    /** Move assignment.

        The moved-from object becomes an empty field sequence.

        @param other The object to move from.
    */
    basic_headers& operator=(basic_headers&& other);

    /// Copy constructor.
    basic_headers(basic_headers const&);

    /// Copy assignment.
    basic_headers& operator=(basic_headers const&);

    /// Copy constructor.
    template<class OtherAlloc>
    basic_headers(basic_headers<OtherAlloc> const&);

    /// Copy assignment.
    template<class OtherAlloc>
    basic_headers& operator=(basic_headers<OtherAlloc> const&);

    /// Construct from a field sequence.
    template<class FwdIt>
    basic_headers(FwdIt first, FwdIt last);

    /// Returns `true` if the field sequence contains no elements.
    bool
    empty() const
    {
        return set_.empty();
    }

    /// Returns the number of elements in the field sequence.
    std::size_t
    size() const
    {
        return set_.size();
    }

    /// Returns `true` if the specified field exists.
    bool
    exists(boost::string_ref const& name) const
    {
        return set_.find(name, less{}) != set_.end();
    }

    /// Returns the number of values for the specified field.
    std::size_t
    count(boost::string_ref const& name) const;

    /** Returns an iterator to the case-insensitive matching field name.

        If more than one field with the specified name exists, the
        first field defined by insertion order is returned.
    */
    iterator
    find(boost::string_ref const& name) const;

    /** Returns the value for a case-insensitive matching header, or `""`.

        If more than one field with the specified name exists, the
        first field defined by insertion order is returned.
    */
    boost::string_ref
    operator[](boost::string_ref const& name) const;

    /// Clear the contents of the basic_headers.
    void
    clear() noexcept;

    /** Remove a field.

        If more than one field with the specified name exists, all
        matching fields will be removed.

        @param name The name of the field(s) to remove.

        @return The number of fields removed.
    */
    std::size_t
    erase(boost::string_ref const& name);

    /** Insert a field value.

        If a field with the same name already exists, the
        existing field is untouched and a new field value pair
        is inserted into the container.

        @param name The name of the field.

        @param value A string holding the value of the field.
    */
    void
    insert(boost::string_ref const& name, boost::string_ref value);

    /** Insert a field value.

        If a field with the same name already exists, the
        existing field is untouched and a new field value pair
        is inserted into the container.

        @param name The name of the field

        @param value The value of the field. The object will be
        converted to a string using `boost::lexical_cast`.
    */
    template<class T>
    typename std::enable_if<
        ! std::is_constructible<boost::string_ref, T>::value>::type
    insert(boost::string_ref name, T const& value)
    {
        insert(name, boost::lexical_cast<std::string>(value));
    }

    /** Replace a field value.

        First removes any values with matching field names, then
        inserts the new field value.

        @param name The name of the field.

        @param value A string holding the value of the field.
    */
    void
    replace(boost::string_ref const& name, boost::string_ref value);

    /** Replace a field value.

        First removes any values with matching field names, then
        inserts the new field value.

        @param name The name of the field

        @param value The value of the field. The object will be
        converted to a string using `boost::lexical_cast`.
    */
    template<class T>
    typename std::enable_if<
        ! std::is_constructible<boost::string_ref, T>::value>::type
    replace(boost::string_ref const& name, T const& value)
    {
        replace(name,
            boost::lexical_cast<std::string>(value));
    }
};

} // http
} // beast

#include <beast/http/impl/basic_headers.ipp>

#endif
