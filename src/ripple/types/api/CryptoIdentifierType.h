//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_CRYPTOIDENTIFIERTYPE_H_INCLUDED
#define RIPPLE_TYPES_CRYPTOIDENTIFIERTYPE_H_INCLUDED

#include <functional>
#include <iterator>
#include <ios>
#include <sstream>
#include <string>

namespace ripple {

//------------------------------------------------------------------------------

/** Template for generalizing the cryptographic primitives used. */
template <class Traits>
class CryptoIdentifierType : public Traits::base
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
        std::size_t operator() (CryptoIdentifierType const& id) const
            { return m_hasher(id.value()); }
    private:
        typename Traits::hasher m_hasher;
    };

    /** Wraps Traits::equal. */
    class equal
    {
    public:
        equal()
            { }
        template <typename Arg>
        equal (Arg arg) : m_equal (arg)
            { }
        bool operator() (CryptoIdentifierType const& lhs,
                         CryptoIdentifierType const& rhs) const
            { return m_equal (lhs.value(), rhs.value()); }
    private:
        typename Traits::equal m_equal;
    };

    /** Create an uninitialized value. */
    CryptoIdentifierType ()
    {
    }

    /** Create a copy from another value . */
    CryptoIdentifierType (value_type const& value)
        : m_value (value)
    {
    }

    /** Create a copy of the value from range of bytes. */
    CryptoIdentifierType (uint8 const* begin, uint8 const* end)
    {
        Traits::construct (begin, end, m_value);
    }

    /** Conversion construction from any specialized type. */
    template <typename Other>
    explicit CryptoIdentifierType (Other const& other)
    {
        this->operator= (other);
    }

    /** Assign a copy from another value. */
    CryptoIdentifierType& operator= (value_type const& value)
    {
        m_value = value;
        return *this;
    }

    /** Copy conversion from any specialized type. */
    template <typename Other>
    CryptoIdentifierType& operator= (Other const& other)
    {
        typename Traits::template assign <Other> () (
            m_value, other);
        return *this;
    }

    /** Access the value. */
    value_type const& value() const
    {
        return m_value;
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

private:
    value_type m_value;
};

//------------------------------------------------------------------------------

template <class Traits>
std::ostream& operator<< (std::ostream& os,
                          CryptoIdentifierType <Traits> const& id)
{
    os << id.to_string();
    return os;
}

template <class Traits>
std::istream& operator>> (std::istream& is,
                          CryptoIdentifierType <Traits> const& id)
{
    return is;
}

//------------------------------------------------------------------------------

}

namespace std {

/** Specialization for hash. */
template <class Traits>
struct hash <ripple::CryptoIdentifierType <Traits> >
{
public:
    typedef ripple::CryptoIdentifierType <Traits> argument_type;
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
struct equal_to <ripple::CryptoIdentifierType <Traits> >
{
public:
    typedef bool                                  result_type;
    typedef ripple::CryptoIdentifierType <Traits> argument_type;
    typedef argument_type                         first_argument_type;
    typedef argument_type                         second_argument_type;

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
    typename argument_type::equal m_equal;
};

}

#endif
