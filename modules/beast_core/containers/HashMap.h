//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_HASHMAP_H_INCLUDED
#define BEAST_HASHMAP_H_INCLUDED

/** The integral type for holding a non cryptographic hash.
    HashValue is used for fast comparisons, bloom filters, and hash maps.
*/
typedef uint32 HashValue;

//------------------------------------------------------------------------------

/** Simple hash functions for use with HashMap.

    @see HashMap
*/
// VFALCO TODO Rewrite the hash functions to return a uint32, and not
//             take the upperLimit parameter. Just do the mod in the
//             calling function for simplicity.
class DefaultHashFunctions
{
public:
    /** Generates a simple hash from an integer. */
    HashValue generateHash (const int key) const noexcept
    {
        return HashValue (std::abs (key));
    }

    /** Generates a simple hash from an int64. */
    HashValue generateHash (const int64 key) const noexcept
    {
        return HashValue (key);
    }

    /** Generates a simple hash from a string. */
    HashValue generateHash (const String& key) const noexcept
    {
        return HashValue (key.hashCode ());
    }

    /** Generates a simple hash from a variant. */
    HashValue generateHash (const var& key) const noexcept
    {
        return generateHash (key.toString ());
    }
};

#if 0
/** Hardened hash functions for use with HashMap.

    The seed is used to make the hash unpredictable. This prevents
    attackers from exploiting crafted inputs to produce degenerate
    containers.
*/
class HardenedHashFunctions
{
public:
    /** Construct a hash function.

        If a seed is specified it will be used, else a random seed
        will be generated from the system.

        @param seedToUse An optional seed to use.
    */
    explicit HardenedHashFunctions (int seedToUse = Random::getSystemRandom ().nextInt ())
        : m_seed (seedToUse)
    {
    }

    // VFALCO TODO Need hardened versions of these functions which use the seed!

private:
    int m_seed;
};
#endif

//------------------------------------------------------------------------------

namespace detail
{

struct BucketTag { };

template <typename M, typename I>
class HashMapLocalIterator
    : public std::iterator <std::forward_iterator_tag, typename M::size_type>
{
public:
    typedef typename M::Pair      value_type;
    typedef value_type*           pointer;
    typedef value_type&           reference;
    typedef typename M::size_type size_type;

    HashMapLocalIterator (M* map = nullptr, I iter = I ())
        : m_map (map)
        , m_iter (iter)
    {
    }

    template <typename N, typename J>
    HashMapLocalIterator (HashMapLocalIterator <N, J> const& other)
        : m_map (other.m_map)
        , m_iter (other.m_iter)
    {
    }

    template <typename N, typename J>
    HashMapLocalIterator& operator= (HashMapLocalIterator <N, J> const& other)
    {
        m_map = other.m_map;
        m_iter = other.m_iter;
        return *this;
    }

    template <typename N, typename J>
    bool operator== (HashMapLocalIterator <N, J> const& other)
    {
        return m_map == other.m_map && m_iter == other.m_iter;
    }

    template <typename N, typename J>
    bool operator!= (HashMapLocalIterator <N, J> const& other)
    {
        return ! ((*this)==other);
    }

    reference operator* () const noexcept
    {
        return dereference ();
    }

    pointer operator-> () const noexcept
    {
        return &dereference ();
    }

    HashMapLocalIterator& operator++ () noexcept
    {
        increment ();
        return *this;
    }

    HashMapLocalIterator operator++ (int) noexcept
    {
        HashMapLocalIterator const result (*this);
        increment ();
        return result;
    }

private:
    reference dereference () const noexcept
    {
        return m_iter->pair ();
    }

    void increment () noexcept
    {
        ++m_iter;
    }

    M* m_map;
    I m_iter;
};

//------------------------------------------------------------------------------


template <typename M>
class HashMapIterator
    : public std::iterator <std::forward_iterator_tag, typename M::size_type>
{
private:
    typedef typename M::Item Item;
    typedef typename M::Bucket Bucket;
    typedef detail::ListIterator <typename mpl::CopyConst <M,
        typename List <Bucket>::Node>::type> bucket_iterator;
    typedef detail::ListIterator <typename mpl::CopyConst <M,
        typename List <Item, detail::BucketTag>::Node>::type> item_iterator;

public:
    typedef typename M::Pair      value_type;
    typedef value_type*           pointer;
    typedef value_type&           reference;
    typedef typename M::size_type size_type;

    HashMapIterator ()
        : m_map (nullptr)
        , m_bucket (bucket_iterator ())
        , m_local (item_iterator ())
    {
    }

    // represents end()
    explicit HashMapIterator (M* map)
        : m_map (map)
        , m_bucket (bucket_iterator ())
        , m_local (item_iterator ())
    {
    }

    HashMapIterator (M* map, bucket_iterator const& bucket, item_iterator const& local)
        : m_map (map)
        , m_bucket (bucket)
        , m_local (local)
    {
    }

#if 0
    HashMapIterator (HashMapIterator const& other) noexcept
        : m_map (other.m_map)
        , m_bucket (other.m_bucket)
        , m_local (other.m_local)
    {
    }

    template <typename N>
    HashMapIterator (HashMapIterator <N> const& other) noexcept
        : m_map (other.m_map)
        , m_bucket (other.m_bucket)
        , m_local (other.m_local)
    {
    }
#endif
    template <typename N>
    HashMapIterator& operator= (HashMapIterator <N> const& other) noexcept
    {
        m_map = other.m_map;
        m_bucket = other.m_bucket;
        m_local = other.m_local;
        return *this;
    }

    template <typename N>
    bool operator== (HashMapIterator <N> const& other) noexcept
    {
        return m_map == other.m_map &&
               m_bucket == other.m_bucket &&
               m_local == other.m_local;
    }

    template <typename N>
    bool operator!= (HashMapIterator <N> const& other) noexcept
    {
        return ! ((*this) == other);
    }

    reference operator* () const noexcept
    {
        return dereference ();
    }

    pointer operator-> () const noexcept
    {
        return &dereference ();
    }

    HashMapIterator& operator++ () noexcept
    {
        increment ();
        return *this;
    }

    HashMapIterator operator++ (int) noexcept
    {
        HashMapIterator const result (*this);
        increment ();
        return result;
    }

private:
    template <typename, typename, typename, typename, typename>
    friend class HashMap;

    reference dereference () const noexcept
    {
        return m_local->pair ();
    }

    void increment () noexcept
    {
        ++m_local;
        if (m_local == m_bucket->items.end ())
        {
            ++m_bucket;
            if (m_bucket != m_map->m_bucketlist.end ())
                m_local = m_bucket->items.begin ();
        }
    }

    M* m_map;
    bucket_iterator m_bucket;
    item_iterator m_local;
};

}

//------------------------------------------------------------------------------

/** Associative container mapping Key to T pairs.
*/
template <typename Key,
          typename T,
          typename Hash = DefaultHashFunctions,
          typename KeyEqual = std::equal_to <Key>,
          typename Allocator = std::allocator <char> >
class HashMap
{
private:
    typedef PARAMETER_TYPE (Key) KeyParam;
    typedef PARAMETER_TYPE (T)   TParam;

public:
    struct Pair
    {
        explicit Pair (Key key)
            : m_key (key)
        {
        }

        Pair (Key key, T t)
            : m_key (key)
            , m_t (t)
        {
        }

        Key const& key () const noexcept
        {
            return m_key;
        }

        T& value () noexcept
        {
            return m_t;
        }

        T const& value () const noexcept
        {
            return m_t;
        }

    private:
        Key m_key;
        T m_t;
    };

private:
    template <typename M, typename I>
    friend class detail::HashMapLocalIterator;

    class Item;

    // Each non-empty bucket is in the linked list.
    struct Bucket : List <Bucket>::Node
    {
        Bucket ()
        {
        }

        inline bool empty () const noexcept
        {
            return items.empty ();
        }

        List <Item, detail::BucketTag> items;

    private:
        Bucket& operator= (Bucket const&);
        Bucket (Bucket const&);
    };

    // Every item in the map is in one linked list
    struct Item 
        : List <Item>::Node
        , List <Item, detail::BucketTag>::Node
    {
        Item (Pair const& pair_)
            : m_pair (pair_)
        {
        }

        Pair& pair () noexcept
        {
            return m_pair;
        }

        Pair const& pair () const noexcept
        {
            return m_pair;
        }

    private:
        Pair m_pair;
    };

public:
    typedef Key               key_type;
    typedef T                 mapped_type;
    typedef Pair              value_type;
    typedef std::size_t       size_type;
    typedef std::ptrdiff_t    difference_type;
    typedef Hash              hasher;
    typedef KeyEqual          key_equal;
    typedef Allocator         allocator_type;
    typedef value_type*       pointer;
    typedef value_type&       reference;
    typedef value_type const* const_pointer;
    typedef value_type const& const_reference;
    
    typedef HashMap <Key, T, Hash, KeyEqual, Allocator> container_type;

    typedef detail::HashMapIterator <container_type> iterator;
    typedef detail::HashMapIterator <container_type const> const_iterator;

    typedef detail::HashMapLocalIterator <container_type,
        typename List <Item, detail::BucketTag>::iterator> local_iterator;

    typedef detail::HashMapLocalIterator <container_type const,
        typename List <Item, detail::BucketTag>::const_iterator> const_local_iterator;

    //--------------------------------------------------------------------------

    enum
    {
        initialBucketCount = 101,
        percentageIncrease = 25
    };

    static float getDefaultLoadFactor () noexcept
    {
        return 1.2f;
    }

    explicit HashMap (
        size_type bucket_count = initialBucketCount,
        KeyEqual const& equal = KeyEqual (),
        Hash const& hash = Hash (),
        Allocator const& allocator = Allocator ())
       : m_hash (hash)
       , m_equal (equal)
       , m_allocator (allocator)
       , m_max_load_factor (getDefaultLoadFactor ())
    {
        rehash (bucket_count);
    }

    HashMap (
        size_type bucket_count,
        Allocator const& allocator = Allocator ())
       : m_allocator (allocator)
       , m_max_load_factor (getDefaultLoadFactor ())
    {
        rehash (bucket_count);
    }

    HashMap (
        size_type bucket_count,
        Hash const& hash = Hash (),
        Allocator const& allocator = Allocator ())
       : m_hash (hash)
       , m_allocator (allocator)
       , m_max_load_factor (getDefaultLoadFactor ())
    {
        rehash (bucket_count);
    }

    explicit HashMap (Allocator const& allocator)
       : m_allocator (allocator)
       , m_max_load_factor (getDefaultLoadFactor ())
    {
        rehash (initialBucketCount);
    }

    ~HashMap()
    {
        clear ();
    }

    HashMap& operator= (HashMap const& other)
    {
        clear ();
        for (iterator iter = other.begin (); iter != other.end (); ++iter)
            (*this)[iter->key ()] = iter->value ();
        return *this;
    }

    allocator_type get_allocator () const noexcept
    {
        return m_allocator;
    }

    //--------------------------------------------------------------------------

    iterator begin () noexcept
    {
        if (m_bucketlist.size () > 0)
            return iterator (this, m_bucketlist.begin (),
                m_bucketlist.front ().items.begin ());
        return end ();
    }

    const_iterator begin () const noexcept
    {
        if (m_bucketlist.size () > 0)
            return const_iterator (this, m_bucketlist.begin (),
                m_bucketlist.front ().items.begin ());
        return end ();
    }

    const_iterator cbegin () const noexcept
    {
        if (m_bucketlist.size () > 0)
            return const_iterator (this, m_bucketlist.begin (),
                m_bucketlist.front ().items.begin ());
        return end ();
    }

    iterator end () noexcept
    {
        return iterator (static_cast <container_type*> (this));
    }

    const_iterator end () const noexcept
    {
        return const_iterator (this);
    }

    const_iterator cend () const noexcept
    {
        return const_iterator (this);
    }

    //--------------------------------------------------------------------------

    bool empty () const noexcept
    {
        return size () == 0;
    }

    size_type size () const noexcept
    {
        return m_itemlist.size ();
    }

    size_type max_size () const noexcept
    {
        return std::numeric_limits <size_type>::max ();
    }

    //--------------------------------------------------------------------------

    void clear()
    {
        for (typename DynamicList <Item>::iterator iter = m_items.begin ();
            iter != m_items.end ();)
        {
            typename DynamicList <Item>::iterator const cur (iter++);
            m_items.erase (cur);
        }

        m_itemlist.clear ();
        m_bucketlist.clear ();
        size_type const count (m_buckets.size ());
        m_buckets.assign (count);
    }

    struct Result
    {
        Result (iterator iter_ = iterator (), bool inserted_ = false)
            : iter (iter_)
            , inserted (inserted_)
        {
        }

        iterator iter;
        bool inserted;
    };

    Result insert (Pair const& p)
    {
        size_type const n (bucket (p.key ()));
        iterator iter (find (p.key (), n));
        if (iter != end ())
            return Result (iter, false);
        check_load ();
        return Result (store (*m_items.emplace_back (p), n), true);
    }

    Result insert (KeyParam key)
    {
        return insert (Pair (key));
    }

    iterator erase (const_iterator pos)
    {
        iterator iter = pos;
        ++iter;
        Bucket& b (*pos.m_iter);
        erase (b, pos->m_local);
        return iter;
    }

    size_type erase (KeyParam key)
    {
        size_type found (0);
        Bucket& b (m_buckets [bucket (key)]);
        for (typename List <Item, detail::BucketTag>::iterator iter (b.items.begin ());
            iter != b.items.end ();)
        {
            typename List <Item, detail::BucketTag>::iterator cur (iter++);
            if (m_equal (cur->pair ().key (), key))
            {
                erase (b, cur);
                ++found;
            }
        }
        return found;
    }

    //--------------------------------------------------------------------------

    T& at (KeyParam key)
    {
        iterator const iter (find (key));
        if (iter == end ())
            Throw (std::out_of_range ("key not found"), __FILE__, __LINE__);
        return iter->value ();
    }

    T const& at (KeyParam key) const
    {
        const_iterator const iter (find (key));
        if (iter == end ())
            Throw (std::out_of_range ("key not found"), __FILE__, __LINE__);
        return iter->value ();
    }

    T& operator[] (KeyParam key) noexcept
    {
        return insert (key).iter->value ();
    }

    size_type count (KeyParam key) const noexcept
    {
        size_type n = 0;
        Bucket const& b (m_buckets [bucket (key)]);
        for (typename List <Item, detail::BucketTag>::iterator iter = b.items.begin ();
            iter != b.items.end (); ++iter)
            if (m_equal (iter->key (), key))
                ++n;
        return n;
    }

    iterator find (KeyParam key) noexcept
    {
        return find (key, bucket (key));
    }

    const_iterator find (KeyParam key) const noexcept
    {
        return find (key, bucket (key));
    }

    //--------------------------------------------------------------------------

    local_iterator begin (size_type n) noexcept
    {
        return local_iterator (this, m_buckets [n].items.begin ());
    }

    const_local_iterator begin (size_type n) const noexcept
    {
        return const_local_iterator (this, m_buckets [n].items.begin ());
    }

    const_local_iterator cbegin (size_type n) const noexcept
    {
        return const_local_iterator (this, m_buckets [n].items.cbegin ());
    }

    local_iterator end (size_type n) noexcept
    {
        return local_iterator (this, m_buckets [n].items.end ());
    }

    const_local_iterator end (size_type n) const noexcept
    {
        return const_local_iterator (this, m_buckets [n].items.end ());
    }

    const_local_iterator cend (size_type n) const noexcept
    {
        return const_local_iterator (this, m_buckets [n].items.cend ());
    }

    size_type bucket_count () const noexcept
    {
        return m_buckets.size ();
    }

    size_type max_bucket_count () const noexcept
    {
        return std::numeric_limits <size_type>::max ();
    }

    size_type bucket_size (size_type n) const noexcept
    {
        return m_buckets [n].items.size ();
    }

    size_type bucket (KeyParam key) const noexcept
    {
        HashValue const hash (m_hash.generateHash (key));
        return hash % bucket_count ();
    }

    //--------------------------------------------------------------------------

    float load_factor () const noexcept
    {
        return float (m_items.size ()) / float (m_buckets.size ());
    }

    float max_load_factor () const noexcept
    {
        return m_max_load_factor;
    }

    void max_load_factor (float ml) noexcept
    {
        m_max_load_factor = ml;
        check_load ();
    }

    void rehash (size_type const count)
    {
        m_bucketlist.clear ();
        m_buckets.assign (count);
        for (typename List <Item>::iterator iter = m_itemlist.begin ();
            iter != m_itemlist.end (); ++iter)
        {
            Item& item (*iter);
            size_type const n (bucket (item.pair ().key ()));
            Bucket& b (m_buckets [n]);
            if (b.empty ())
                m_bucketlist.push_front (b);
            b.items.push_front (item);
        }
    }

    void reserve (size_type count)
    {
        m_items.reserve (count);
        rehash (std::ceil (count / max_load_factor ()));
    }

private:
    // rehashes if adding one more item would put us over
    void check_load () noexcept
    {
        if ( (float (m_items.size () + 1) /
              float (m_buckets.size ())) >=
              max_load_factor ())
        {
            grow_buckets ();
        }
    }

    void grow_buckets ()
    {
        double const scale = 1. + (double (percentageIncrease) / 100.);
        size_type const count (size_type (std::ceil (
            (double (size ()) / double (max_load_factor ())) * scale)));
        rehash (count);
    }

    iterator find (KeyParam key, size_type n) noexcept
    {
        Bucket& b (m_buckets [n]);
        for (typename List <Item, detail::BucketTag>::iterator iter =
            b.items.begin (); iter != b.items.end (); ++iter)
            if (m_equal (iter->pair ().key (), key))
                return iterator (this, m_bucketlist.iterator_to (b), iter);
        return end ();
    }

    const_iterator find (KeyParam key, size_type n) const noexcept
    {
        Bucket const& b (m_buckets [n]);
        for (typename List <Item, detail::BucketTag>::const_iterator iter =
            b.items.begin (); iter != b.items.end (); ++iter)
            if (m_equal (iter->pair ().key (), key))
                return const_iterator (this,
                    m_bucketlist.const_iterator_to (b), iter);
        return end ();
    }

    iterator store (Item& item, size_type n)
    {
        check_load ();
        Bucket& b (m_buckets [n]);
        if (b.empty ())
            m_bucketlist.push_front (b);
        b.items.push_front (item);
        m_itemlist.push_front (item);
        return iterator (this,
            m_bucketlist.iterator_to (b),
                b.items.begin ());
    }

    void erase (Bucket& b, typename List <Item, detail::BucketTag>::iterator pos)
    {
        Item& item (*pos);
        b.items.erase (b.items.iterator_to (item));
        if (b.empty ())
            m_bucketlist.erase (m_bucketlist.iterator_to (b));
        m_itemlist.erase (m_itemlist.iterator_to (item));
        m_items.erase (m_items.iterator_to (item));
    }

private:
    template <typename M>
    friend class detail::HashMapIterator;

    Hash m_hash;
    KeyEqual m_equal;
    Allocator m_allocator;
    DynamicList <Item> m_items;
    DynamicArray <Bucket> m_buckets;
    List <Item> m_itemlist;
    List <Bucket> m_bucketlist;
    float m_max_load_factor;
};

#endif

