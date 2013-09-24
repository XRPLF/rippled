//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_CRYPTOIDENTIFIERSTORAGE_H_INCLUDED
#define RIPPLE_TYPES_CRYPTOIDENTIFIERSTORAGE_H_INCLUDED

#include "beast/beast/FixedArray.h"

namespace ripple {

/** A padded FixedArray used with CryptoIdentifierType traits. */
template <std::size_t PreSize, std::size_t Size, std::size_t PostSize>
class CryptoIdentifierStorage
{
public:
    typedef std::size_t         size_type;
    typedef std::ptrdiff_t      difference_type;
    typedef uint8               value_type;
    typedef value_type*         iterator;
    typedef value_type const*   const_iterator;
    typedef value_type&         reference;
    typedef value_type const&   const_reference;

    static size_type const      pre_size = PreSize;
    static size_type const      size = Size;
    static size_type const      post_size = PostSize;
    static size_type const      storage_size = pre_size + size + post_size;

    typedef FixedArray <
        uint8, storage_size>    storage_type;

    /** Value hashing function.
        The seed prevents crafted inputs from causing degenarate parent containers.
    */
    class hasher
    {
    public:
        explicit hasher (HashValue seedToUse = Random::getSystemRandom ().nextInt ())
            : m_seed (seedToUse)
        {
        }

        std::size_t operator() (CryptoIdentifierStorage const& storage) const
        {
            std::size_t hash;
            Murmur::Hash (storage.cbegin (), storage.size, m_seed, &hash);
            return hash;
        }

    private:
        std::size_t m_seed;
    };

    /** Container equality testing function. */
    class equal
    {
    public:
        bool operator() (CryptoIdentifierStorage const& lhs,
                         CryptoIdentifierStorage const& rhs) const
        {
            return lhs == rhs;
        }
    };        

    // iterator access
    iterator        begin()       { return &m_storage[pre_size]; }
    const_iterator  begin() const { return &m_storage[pre_size]; }
    const_iterator cbegin() const { return &m_storage[pre_size]; }
    iterator        end()       { return &m_storage[storage_size-post_size]; }
    const_iterator  end() const { return &m_storage[storage_size-post_size]; }
    const_iterator cend() const { return &m_storage[storage_size-post_size]; }

    typedef std::reverse_iterator <iterator> reverse_iterator;
    typedef std::reverse_iterator <const_iterator> const_reverse_iterator;

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const { return const_reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
    const_reverse_iterator crend() const { return const_reverse_iterator(begin()); }

    reference operator[](size_type i)
    {
        bassert (i < size);
        return m_storage[pre_size+i];
    }

    const_reference operator[](size_type i) const
    {
        bassert (i < size);
        return m_storage[pre_size+i];
    }

    reference front() { return m_storage[pre_size]; }
    reference back()  { return m_storage[storage_size-post_size-1]; }
    const_reference front () const { return m_storage[pre_size]; }
    const_reference back()   const { return m_storage[storage_size-post_size-1]; }

    value_type const* data() const { return &m_storage[pre_size]; }
    value_type* data() { return &m_storage[pre_size]; }
    value_type* c_array() { return &m_storage[pre_size]; }

    void assign (value_type value) { fill (value); }
    void fill (value_type value)   { std::fill_n (begin(), size, value); }
    void clear ()                  { fill (value_type ()); }

    // Access storage
    storage_type&       storage()       { return m_storage; }
    storage_type const& storage() const { return m_storage; }

    void rangecheck (size_type i)
    {
        if (i >= size)
            throw std::out_of_range ("CryptoIdentifierStorage<>: index out of range");
    }

private:
    storage_type m_storage;
};

//------------------------------------------------------------------------------

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator== (CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return std::equal (lhs.begin(), lhs.end(), rhs.begin());
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator!= (CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return !(lhs==rhs);
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator< (CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
    CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return std::lexicographical_compare (lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator> (CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return rhs<lhs;
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator<= (CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return !(rhs<lhs);
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator>= (CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 CryptoIdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return !(lhs<rhs);
}

}

#endif
