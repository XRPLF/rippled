//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_BASIC_FIELDS_HPP
#define BEAST_HTTP_BASIC_FIELDS_HPP

#include <beast/core/detail/empty_base_optimization.hpp>
#include <beast/http/detail/basic_fields.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

/** A container for storing HTTP header fields.

    This container is designed to store the field value pairs that make
    up the fields and trailers in a HTTP message. Objects of this type
    are iterable, with each element holding the field name and field
    value.

    Field names are stored as-is, but comparisons are case-insensitive.
    When the container is iterated, the fields are presented in the order
    of insertion. For fields with the same name, the container behaves
    as a `std::multiset`; there will be a separate value for each occurrence
    of the field name.

    @note Meets the requirements of @b FieldSequence.
*/
template<class Allocator>
class basic_fields :
#if ! GENERATING_DOCS
    private beast::detail::empty_base_optimization<
        typename std::allocator_traits<Allocator>::
            template rebind_alloc<
                detail::basic_fields_base::element>>,
#endif
    public detail::basic_fields_base
{
    using alloc_type = typename
        std::allocator_traits<Allocator>::
            template rebind_alloc<
                detail::basic_fields_base::element>;

    using alloc_traits =
        std::allocator_traits<alloc_type>;

    using size_type =
        typename std::allocator_traits<Allocator>::size_type;

    void
    delete_all();

    void
    move_assign(basic_fields&, std::false_type);

    void
    move_assign(basic_fields&, std::true_type);

    void
    copy_assign(basic_fields const&, std::false_type);

    void
    copy_assign(basic_fields const&, std::true_type);

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

    /** The value type of the field sequence.

        Meets the requirements of @b Field.
    */
#if GENERATING_DOCS
    using value_type = implementation_defined;
#endif

    /// A const iterator to the field sequence
#if GENERATING_DOCS
    using iterator = implementation_defined;
#endif

    /// A const iterator to the field sequence
#if GENERATING_DOCS
    using const_iterator = implementation_defined;
#endif

    /// Default constructor.
    basic_fields() = default;

    /// Destructor
    ~basic_fields();

    /** Construct the fields.

        @param alloc The allocator to use.
    */
    explicit
    basic_fields(Allocator const& alloc);

    /** Move constructor.

        The moved-from object becomes an empty field sequence.

        @param other The object to move from.
    */
    basic_fields(basic_fields&& other);

    /** Move assignment.

        The moved-from object becomes an empty field sequence.

        @param other The object to move from.
    */
    basic_fields& operator=(basic_fields&& other);

    /// Copy constructor.
    basic_fields(basic_fields const&);

    /// Copy assignment.
    basic_fields& operator=(basic_fields const&);

    /// Copy constructor.
    template<class OtherAlloc>
    basic_fields(basic_fields<OtherAlloc> const&);

    /// Copy assignment.
    template<class OtherAlloc>
    basic_fields& operator=(basic_fields<OtherAlloc> const&);

    /// Construct from a field sequence.
    template<class FwdIt>
    basic_fields(FwdIt first, FwdIt last);

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

    /// Returns a const iterator to the beginning of the field sequence.
    const_iterator
    begin() const
    {
        return list_.cbegin();
    }

    /// Returns a const iterator to the end of the field sequence.
    const_iterator
    end() const
    {
        return list_.cend();
    }

    /// Returns a const iterator to the beginning of the field sequence.
    const_iterator
    cbegin() const
    {
        return list_.cbegin();
    }

    /// Returns a const iterator to the end of the field sequence.
    const_iterator
    cend() const
    {
        return list_.cend();
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

    /// Clear the contents of the basic_fields.
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

#include <beast/http/impl/basic_fields.ipp>

#endif
