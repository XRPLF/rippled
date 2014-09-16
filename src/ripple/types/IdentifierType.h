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

#ifndef RIPPLE_TYPES_IDENTIFIERTYPE_H_INCLUDED
#define RIPPLE_TYPES_IDENTIFIERTYPE_H_INCLUDED

#include <functional>
#include <iterator>
#include <ios>
#include <sstream>
#include <string>

namespace ripple {

//------------------------------------------------------------------------------

/** Template for generalizing the cryptographic primitives used. */
template <class Traits>
class IdentifierType : public Traits::base
{
public:
    static std::size_t const                            size = Traits::size;
    typedef typename Traits::value_type                 value_type;
    typedef typename value_type::const_iterator         const_iterator;
    typedef typename value_type::const_reverse_iterator const_reverse_iterator;

    /** Wraps Traits::hasher. */
    class hasher
    {
    public:
        hasher()
            { }
        template <typename Arg>
        hasher (Arg arg) : m_hasher (arg)
            { }
        std::size_t operator() (IdentifierType const& id) const noexcept
            { return m_hasher(id.value()); }
    private:
        typename Traits::hasher m_hasher;
    };

    /** Wraps Traits::equal. */
    class key_equal
    {
    public:
        key_equal()
            { }
        template <typename Arg>
        key_equal (Arg arg) : m_equal (arg)
            { }
        bool operator() (IdentifierType const& lhs,
                         IdentifierType const& rhs) const noexcept
            { return m_equal (lhs.value(), rhs.value()); }
    private:
        typename Traits::key_equal m_equal;
    };

    /** Create an uninitialized value. */
    IdentifierType ()
        { }

    /** Implicit conversion construction from value_type.
        This allows the IdentifierType to appear as an lvalue where the
        rvalue is the underlying container type.
    */
    IdentifierType (value_type const& value)
        : m_value (value)
        { }

    /** Create a copy of the value from range of bytes. */
    IdentifierType (std::uint8_t const* begin, std::uint8_t const* end)
        { Traits::construct (begin, end, m_value); }

    /** Conversion construction from any specialized type. */
    template <typename Other>
    explicit IdentifierType (Other const& other)
    {
        this->operator= (other);
    }

    /** Assign a copy from another value. */
    IdentifierType& operator= (value_type const& value)
    {
        m_value = value;
        return *this;
    }

    /** Copy conversion from any specialized type. */
    template <typename Other>
    IdentifierType& operator= (Other const& other)
    {
        typename Traits::template assign <Other> () (
            m_value, other);
        return *this;
    }

    /** Access the value. */
    value_type const& value() const
        { return m_value; }

    /** Smart dereference.
        This provides access to the underlying container for compatibility.
        For example, to call member functions that are otherwise not
        available.
    */
    /** @{ */
    value_type const* operator->() const
        { return &value(); }
    value_type const& operator*() const
        { return value(); }
    /** @} */

    /** Implicit conversion to value_type.
        This lets the IdentifierType appear as an rvalue in an assignment
        where the lvalue is of type value_type.
    */
    operator value_type const& () const
    {
        return value();
    }

    /** Iterator access. */
    /** @{ */
    const_iterator begin()  const { return value().begin(); }
    const_iterator end()    const { return value().end(); }
    const_iterator cbegin() const { return value().cbegin(); }
    const_iterator cend()   const { return value().cend(); }
    const_reverse_iterator rbegin()  const { return value().rbegin(); }
    const_reverse_iterator rend()    const { return value().rend(); }
    const_reverse_iterator crbegin() const { return value().crbegin(); }
    const_reverse_iterator crend()   const { return value().crend(); }
    /** @} */

    /** Conversion to std::string. */
    std::string to_string() const
    {
        return Traits::to_string (m_value);
    }

    /** Conversion from std::string.
        The `bool` indicates the success of the conversion.
    */
    static std::pair <IdentifierType, bool> from_string (std::string const& s)
    {
        return Traits::from_string (s);
    }


private:
    value_type m_value;
};

template <class Hasher, class Traits>
inline
void
hash_append(Hasher& h, IdentifierType<Traits> const& id)
{
    using beast::hash_append;
    hash_append (h, id.value());
}

//------------------------------------------------------------------------------

template <class Traits>
bool operator== (IdentifierType<Traits> const& lhs,
                 IdentifierType<Traits> const& rhs)
{ return lhs.value() == rhs.value(); }

template <class Traits>
bool operator!= (IdentifierType<Traits> const& lhs,
                 IdentifierType<Traits> const& rhs)
{ return lhs.value() != rhs.value(); }

template <class Traits>
bool operator< (IdentifierType<Traits> const& lhs,
    IdentifierType<Traits> const& rhs)
{ return lhs.value() < rhs.value(); }

template <class Traits>
bool operator> (IdentifierType<Traits> const& lhs,
                IdentifierType<Traits> const& rhs)
{ return lhs.value() > rhs.value(); }

template <class Traits>
bool operator<= (IdentifierType<Traits> const& lhs,
                 IdentifierType<Traits> const& rhs)
{ return lhs.value() <= rhs.value(); }

template <class Traits>
bool operator>= (IdentifierType<Traits> const& lhs,
                 IdentifierType<Traits> const& rhs)
{ return lhs.value() >= rhs.value(); }

//------------------------------------------------------------------------------

template <class Traits>
std::ostream& operator<< (std::ostream& os,
                          IdentifierType <Traits> const& id)
{
    os << id.to_string();
    return os;
}

template <class Traits>
std::istream& operator>> (std::istream& is,
                          IdentifierType <Traits> const& id)
{
    return is;
}

//------------------------------------------------------------------------------

}

namespace std {

/** Specialization for hash. */
template <class Traits>
struct hash <ripple::IdentifierType <Traits> >
{
public:
    typedef ripple::IdentifierType <Traits> argument_type;
    typedef std::size_t                           result_type;

    hash ()
    {
        static typename argument_type::hasher s_hash;
        m_hash = s_hash;
    }

    template <typename Arg>
    explicit hash (Arg arg)
        : m_hash (arg)
    {
    }

    result_type operator() (argument_type const& key) const
    {
        return m_hash (key);
    }

private:
    typename argument_type::hasher m_hash;
};

//------------------------------------------------------------------------------

/** Specialization for equal_to. */
template <class Traits>
struct equal_to <ripple::IdentifierType <Traits> >
{
public:
    typedef bool                            result_type;
    typedef ripple::IdentifierType <Traits> argument_type;
    typedef argument_type                   first_argument_type;
    typedef argument_type                   second_argument_type;

    equal_to ()
    {
    }

    template <typename Arg>
    explicit equal_to (Arg arg)
        : m_equal (arg)
    {
    }

    result_type operator() (argument_type const& lhs,
                            argument_type const& rhs) const
    {
        return m_equal (lhs, rhs);
    }

private:
    typename argument_type::key_equal m_equal;
};

}

#endif
