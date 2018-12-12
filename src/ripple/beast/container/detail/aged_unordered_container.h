//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CONTAINER_DETAIL_AGED_UNORDERED_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_DETAIL_AGED_UNORDERED_CONTAINER_H_INCLUDED

#include <ripple/beast/container/detail/aged_container_iterator.h>
#include <ripple/beast/container/detail/aged_associative_container.h>
#include <ripple/beast/container/detail/empty_base_optimization.h>
#include <ripple/beast/container/aged_container.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

/*

TODO

- Add constructor variations that take a bucket count

- Review for noexcept and exception guarantees

- Call the safe version of is_permutation that takes 4 iterators

*/

#ifndef BEAST_NO_CXX14_IS_PERMUTATION
#define BEAST_NO_CXX14_IS_PERMUTATION 1
#endif

namespace beast {
namespace detail {

/** Associative container where each element is also indexed by time.

    This container mirrors the interface of the standard library unordered
    associative containers, with the addition that each element is associated
    with a `when` `time_point` which is obtained from the value of the clock's
    `now`. The function `touch` updates the time for an element to the current
    time as reported by the clock.

    An extra set of iterator types and member functions are provided in the
    `chronological` memberspace that allow traversal in temporal or reverse
    temporal order. This container is useful as a building block for caches
    whose items expire after a certain amount of time. The chronological
    iterators allow for fully customizable expiration strategies.

    @see aged_unordered_set, aged_unordered_multiset
    @see aged_unordered_map, aged_unordered_multimap
*/
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock = std::chrono::steady_clock,
    class Hash = std::hash <Key>,
    class KeyEqual = std::equal_to <Key>,
    class Allocator = std::allocator <
        typename std::conditional <IsMap,
            std::pair <Key const, T>,
                Key>::type>
>
class aged_unordered_container
{
public:
    using clock_type = abstract_clock<Clock>;
    using time_point = typename clock_type::time_point;
    using duration = typename clock_type::duration;
    using key_type = Key;
    using mapped_type = T;
    using value_type = typename std::conditional <IsMap,
        std::pair <Key const, T>, Key>::type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    // Introspection (for unit tests)
    using is_unordered = std::true_type;
    using is_multi = std::integral_constant <bool, IsMulti>;
    using is_map = std::integral_constant <bool, IsMap>;

private:
    static Key const& extract (value_type const& value)
    {
        return aged_associative_container_extract_t <IsMap> () (value);
    }

    // VFALCO TODO hoist to remove template argument dependencies
    struct element
        : boost::intrusive::unordered_set_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>
            >
        , boost::intrusive::list_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>
            >
    {
        // Stash types here so the iterator doesn't
        // need to see the container declaration.
        struct stashed
        {
            explicit stashed() = default;

            using value_type = typename aged_unordered_container::value_type;
            using time_point = typename aged_unordered_container::time_point;
        };

        element (
            time_point const& when_,
            value_type const& value_)
            : value (value_)
            , when (when_)
        {
        }

        element (
            time_point const& when_,
            value_type&& value_)
            : value (std::move (value_))
            , when (when_)
        {
        }

        template <
            class... Args,
            class = typename std::enable_if <
                std::is_constructible <value_type,
                    Args...>::value>::type
        >
        element (time_point const& when_, Args&&... args)
            : value (std::forward <Args> (args)...)
            , when (when_)
        {
        }

        value_type value;
        time_point when;
    };

    // VFALCO TODO hoist to remove template argument dependencies
    class ValueHash
        : private beast::detail::empty_base_optimization <Hash>
#ifdef _LIBCPP_VERSION
        , public std::unary_function <element, std::size_t>
#endif
    {
    public:
#ifndef _LIBCPP_VERSION
        using argument_type = element;
        using result_type = size_t;
#endif

        ValueHash ()
        {
        }

        ValueHash (Hash const& hash)
            : beast::detail::empty_base_optimization <Hash> (hash)
        {
        }

        std::size_t operator() (element const& e) const
        {
            return this->member() (extract (e.value));
        }

        Hash& hash_function()
        {
            return this->member();
        }

        Hash const& hash_function() const
        {
            return this->member();
        }
    };

    // Compares value_type against element, used in find/insert_check
    // VFALCO TODO hoist to remove template argument dependencies
    class KeyValueEqual
        : private beast::detail::empty_base_optimization <KeyEqual>
#ifdef _LIBCPP_VERSION
        , public std::binary_function <Key, element, bool>
#endif
    {
    public:
#ifndef _LIBCPP_VERSION
        using first_argument_type = Key;
        using second_argument_type = element;
        using result_type = bool;
#endif

        KeyValueEqual ()
        {
        }

        KeyValueEqual (KeyEqual const& keyEqual)
            : beast::detail::empty_base_optimization <KeyEqual> (keyEqual)
        {
        }

        // VFALCO NOTE WE might want only to enable these overloads
        //                if KeyEqual has is_transparent
#if 0
        template <class K>
        bool operator() (K const& k, element const& e) const
        {
            return this->member() (k, extract (e.value));
        }

        template <class K>
        bool operator() (element const& e, K const& k) const
        {
            return this->member() (extract (e.value), k);
        }
#endif

        bool operator() (Key const& k, element const& e) const
        {
            return this->member() (k, extract (e.value));
        }

        bool operator() (element const& e, Key const& k) const
        {
            return this->member() (extract (e.value), k);
        }

        bool operator() (element const& lhs, element const& rhs) const
        {
            return this->member() (extract (lhs.value), extract (rhs.value));
        }

        KeyEqual& key_eq()
        {
            return this->member();
        }

        KeyEqual const& key_eq() const
        {
            return this->member();
        }
    };

    using list_type = typename boost::intrusive::make_list <element,
        boost::intrusive::constant_time_size <false>>::type;

    using cont_type = typename std::conditional <
        IsMulti,
        typename boost::intrusive::make_unordered_multiset <element,
            boost::intrusive::constant_time_size <true>,
            boost::intrusive::hash <ValueHash>,
            boost::intrusive::equal <KeyValueEqual>,
            boost::intrusive::cache_begin <true>
                    >::type,
        typename boost::intrusive::make_unordered_set <element,
            boost::intrusive::constant_time_size <true>,
            boost::intrusive::hash <ValueHash>,
            boost::intrusive::equal <KeyValueEqual>,
            boost::intrusive::cache_begin <true>
                    >::type
        >::type;

    using bucket_type = typename cont_type::bucket_type;
    using bucket_traits = typename cont_type::bucket_traits;

    using ElementAllocator = typename std::allocator_traits <
        Allocator>::template rebind_alloc <element>;

    using ElementAllocatorTraits = std::allocator_traits <ElementAllocator>;

    using BucketAllocator = typename std::allocator_traits <
        Allocator>::template rebind_alloc <element>;

    using BucketAllocatorTraits = std::allocator_traits <BucketAllocator>;

    class config_t
        : private ValueHash
        , private KeyValueEqual
        , private beast::detail::empty_base_optimization <ElementAllocator>
    {
    public:
        explicit config_t (
            clock_type& clock_)
            : clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash)
            : ValueHash (hash)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            KeyEqual const& keyEqual)
            : KeyValueEqual (keyEqual)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Allocator const& alloc_)
            : beast::detail::empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash,
            KeyEqual const& keyEqual)
            : ValueHash (hash)
            , KeyValueEqual (keyEqual)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash,
            Allocator const& alloc_)
            : ValueHash (hash)
            , beast::detail::empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            KeyEqual const& keyEqual,
            Allocator const& alloc_)
            : KeyValueEqual (keyEqual)
            , beast::detail::empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (
            clock_type& clock_,
            Hash const& hash,
            KeyEqual const& keyEqual,
            Allocator const& alloc_)
            : ValueHash (hash)
            , KeyValueEqual (keyEqual)
            , beast::detail::empty_base_optimization <ElementAllocator> (alloc_)
            , clock (clock_)
        {
        }

        config_t (config_t const& other)
            : ValueHash (other.hash_function())
            , KeyValueEqual (other.key_eq())
            , beast::detail::empty_base_optimization <ElementAllocator> (
                ElementAllocatorTraits::
                    select_on_container_copy_construction (
                        other.alloc()))
            , clock (other.clock)
        {
        }

        config_t (config_t const& other, Allocator const& alloc)
            : ValueHash (other.hash_function())
            , KeyValueEqual (other.key_eq())
            , beast::detail::empty_base_optimization <ElementAllocator> (alloc)
            , clock (other.clock)
        {
        }

        config_t (config_t&& other)
            : ValueHash (std::move (other.hash_function()))
            , KeyValueEqual (std::move (other.key_eq()))
            , beast::detail::empty_base_optimization <ElementAllocator> (
                std::move (other.alloc()))
            , clock (other.clock)
        {
        }

        config_t (config_t&& other, Allocator const& alloc)
            : ValueHash (std::move (other.hash_function()))
            , KeyValueEqual (std::move (other.key_eq()))
            , beast::detail::empty_base_optimization <ElementAllocator> (alloc)
            , clock (other.clock)
        {
        }

        config_t& operator= (config_t const& other)
        {
            hash_function() = other.hash_function();
            key_eq() = other.key_eq();
            alloc() = other.alloc();
            clock = other.clock;
            return *this;
        }

        config_t& operator= (config_t&& other)
        {
            hash_function() = std::move (other.hash_function());
            key_eq() = std::move (other.key_eq());
            alloc() = std::move (other.alloc());
            clock = other.clock;
            return *this;
        }

        ValueHash& value_hash()
        {
            return *this;
        }

        ValueHash const& value_hash() const
        {
            return *this;
        }

        Hash& hash_function()
        {
            return ValueHash::hash_function();
        }

        Hash const& hash_function() const
        {
            return ValueHash::hash_function();
        }

        KeyValueEqual& key_value_equal()
        {
            return *this;
        }

        KeyValueEqual const& key_value_equal() const
        {
            return *this;
        }

        KeyEqual& key_eq()
        {
            return key_value_equal().key_eq();
        }

        KeyEqual const& key_eq() const
        {
            return key_value_equal().key_eq();
        }

        ElementAllocator& alloc()
        {
            return beast::detail::empty_base_optimization <
                ElementAllocator>::member();
        }

        ElementAllocator const& alloc() const
        {
            return beast::detail::empty_base_optimization <
                ElementAllocator>::member();
        }

        std::reference_wrapper <clock_type> clock;
    };

    class Buckets
    {
    public:
        using vec_type = std::vector<
            bucket_type,
            typename std::allocator_traits <Allocator>::
                template rebind_alloc <bucket_type>>;

        Buckets ()
            : m_max_load_factor (1.f)
            , m_vec ()
        {
            m_vec.resize (
                cont_type::suggested_upper_bucket_count (0));
        }

        Buckets (Allocator const& alloc)
            : m_max_load_factor (1.f)
            , m_vec (alloc)
        {
            m_vec.resize (
                cont_type::suggested_upper_bucket_count (0));
        }

        operator bucket_traits()
        {
            return bucket_traits (&m_vec[0], m_vec.size());
        }

        void clear()
        {
            m_vec.clear();
        }

        size_type max_bucket_count() const
        {
            return m_vec.max_size();
        }

        float& max_load_factor()
        {
            return m_max_load_factor;
        }

        float const& max_load_factor() const
        {
            return m_max_load_factor;
        }

        // count is the number of buckets
        template <class Container>
        void rehash (size_type count, Container& c)
        {
            size_type const size (m_vec.size());
            if (count == size)
                return;
            if (count > m_vec.capacity())
            {
                // Need two vectors otherwise we
                // will destroy non-empty buckets.
                vec_type vec (m_vec.get_allocator());
                std::swap (m_vec, vec);
                m_vec.resize (count);
                c.rehash (bucket_traits (
                    &m_vec[0], m_vec.size()));
                return;
            }
            // Rehash in place.
            if (count > size)
            {
                // This should not reallocate since
                // we checked capacity earlier.
                m_vec.resize (count);
                c.rehash (bucket_traits (
                    &m_vec[0], count));
                return;
            }
            // Resize must happen after rehash otherwise
            // we might destroy non-empty buckets.
            c.rehash (bucket_traits (
                &m_vec[0], count));
            m_vec.resize (count);
        }

        // Resize the buckets to accomodate at least n items.
        template <class Container>
        void resize (size_type n, Container& c)
        {
            size_type const suggested (
                cont_type::suggested_upper_bucket_count (n));
            rehash (suggested, c);
        }

    private:
        float m_max_load_factor;
        vec_type m_vec;
    };

    template <class... Args>
    element* new_element (Args&&... args)
    {
        struct Deleter
        {
            std::reference_wrapper <ElementAllocator> a_;
            Deleter (ElementAllocator& a)
                : a_(a)
            {
            }

            void
            operator()(element* p)
            {
                ElementAllocatorTraits::deallocate (a_.get(), p, 1);
            }
        };

        std::unique_ptr <element, Deleter> p (ElementAllocatorTraits::allocate (
            m_config.alloc(), 1), Deleter(m_config.alloc()));
        ElementAllocatorTraits::construct (m_config.alloc(),
            p.get(), clock().now(), std::forward <Args> (args)...);
        return p.release();
    }

    void delete_element (element const* p)
    {
        ElementAllocatorTraits::destroy (m_config.alloc(), p);
        ElementAllocatorTraits::deallocate (
            m_config.alloc(), const_cast<element*>(p), 1);
    }

    void unlink_and_delete_element (element const* p)
    {
        chronological.list.erase (
            chronological.list.iterator_to (*p));
        m_cont.erase (m_cont.iterator_to (*p));
        delete_element (p);
    }

public:
    using hasher = Hash;
    using key_equal = KeyEqual;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = typename std::allocator_traits <
        Allocator>::pointer;
    using const_pointer = typename std::allocator_traits <
        Allocator>::const_pointer;

    // A set iterator (IsMap==false) is always const
    // because the elements of a set are immutable.
    using iterator= beast::detail::aged_container_iterator <!IsMap,
        typename cont_type::iterator>;
    using const_iterator = beast::detail::aged_container_iterator <true,
        typename cont_type::iterator>;

    using local_iterator = beast::detail::aged_container_iterator <!IsMap,
        typename cont_type::local_iterator>;
    using const_local_iterator = beast::detail::aged_container_iterator <true,
        typename cont_type::local_iterator>;

    //--------------------------------------------------------------------------
    //
    // Chronological ordered iterators
    //
    // "Memberspace"
    // http://accu.org/index.php/journals/1527
    //
    //--------------------------------------------------------------------------

    class chronological_t
    {
    public:
        // A set iterator (IsMap==false) is always const
        // because the elements of a set are immutable.
        using iterator = beast::detail::aged_container_iterator <
            ! IsMap, typename list_type::iterator>;
        using const_iterator = beast::detail::aged_container_iterator <
            true, typename list_type::iterator>;
        using reverse_iterator = beast::detail::aged_container_iterator <
            ! IsMap, typename list_type::reverse_iterator>;
        using const_reverse_iterator = beast::detail::aged_container_iterator <
            true, typename list_type::reverse_iterator>;

        iterator begin ()
         {
            return iterator (list.begin());
        }

        const_iterator begin () const
        {
            return const_iterator (list.begin ());
        }

        const_iterator cbegin() const
        {
            return const_iterator (list.begin ());
        }

        iterator end ()
        {
            return iterator (list.end ());
        }

        const_iterator end () const
        {
            return const_iterator (list.end ());
        }

        const_iterator cend () const
        {
            return const_iterator (list.end ());
        }

        reverse_iterator rbegin ()
        {
            return reverse_iterator (list.rbegin());
        }

        const_reverse_iterator rbegin () const
        {
            return const_reverse_iterator (list.rbegin ());
        }

        const_reverse_iterator crbegin() const
        {
            return const_reverse_iterator (list.rbegin ());
        }

        reverse_iterator rend ()
        {
            return reverse_iterator (list.rend ());
        }

        const_reverse_iterator rend () const
        {
            return const_reverse_iterator (list.rend ());
        }

        const_reverse_iterator crend () const
        {
            return const_reverse_iterator (list.rend ());
        }

        iterator iterator_to (value_type& value)
        {
            static_assert (std::is_standard_layout <element>::value,
                "must be standard layout");
            return list.iterator_to (*reinterpret_cast <element*>(
                 reinterpret_cast<uint8_t*>(&value)-((std::size_t)
                    std::addressof(((element*)0)->member))));
        }

        const_iterator iterator_to (value_type const& value) const
        {
            static_assert (std::is_standard_layout <element>::value,
                "must be standard layout");
            return list.iterator_to (*reinterpret_cast <element const*>(
                 reinterpret_cast<uint8_t const*>(&value)-((std::size_t)
                    std::addressof(((element*)0)->member))));
        }

    private:
        chronological_t ()
        {
        }

        chronological_t (chronological_t const&) = delete;
        chronological_t (chronological_t&&) = delete;

        friend class aged_unordered_container;
        list_type mutable list;
    } chronological;

    //--------------------------------------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------------------------------------

    aged_unordered_container() = delete;

    explicit aged_unordered_container (clock_type& clock);

    aged_unordered_container (clock_type& clock, Hash const& hash);

    aged_unordered_container (clock_type& clock,
        KeyEqual const& key_eq);

    aged_unordered_container (clock_type& clock,
        Allocator const& alloc);

    aged_unordered_container (clock_type& clock,
        Hash const& hash, KeyEqual const& key_eq);

    aged_unordered_container (clock_type& clock,
        Hash const& hash, Allocator const& alloc);

    aged_unordered_container (clock_type& clock,
        KeyEqual const& key_eq, Allocator const& alloc);

    aged_unordered_container (
        clock_type& clock, Hash const& hash, KeyEqual const& key_eq,
            Allocator const& alloc);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock, Hash const& hash);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock, KeyEqual const& key_eq);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock, Allocator const& alloc);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock, Hash const& hash, KeyEqual const& key_eq);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock, Hash const& hash, Allocator const& alloc);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock, KeyEqual const& key_eq,
            Allocator const& alloc);

    template <class InputIt>
    aged_unordered_container (InputIt first, InputIt last,
        clock_type& clock, Hash const& hash, KeyEqual const& key_eq,
            Allocator const& alloc);

    aged_unordered_container (aged_unordered_container const& other);

    aged_unordered_container (aged_unordered_container const& other,
        Allocator const& alloc);

    aged_unordered_container (aged_unordered_container&& other);

    aged_unordered_container (aged_unordered_container&& other,
        Allocator const& alloc);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock, Hash const& hash);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock, KeyEqual const& key_eq);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock, Allocator const& alloc);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock, Hash const& hash, KeyEqual const& key_eq);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock, Hash const& hash, Allocator const& alloc);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock, KeyEqual const& key_eq, Allocator const& alloc);

    aged_unordered_container (std::initializer_list <value_type> init,
        clock_type& clock, Hash const& hash, KeyEqual const& key_eq,
            Allocator const& alloc);

    ~aged_unordered_container();

    aged_unordered_container& operator= (aged_unordered_container const& other);

    aged_unordered_container& operator= (aged_unordered_container&& other);

    aged_unordered_container& operator= (std::initializer_list <value_type> init);

    allocator_type get_allocator() const
    {
        return m_config.alloc();
    }

    clock_type& clock()
    {
        return m_config.clock;
    }

    clock_type const& clock() const
    {
        return m_config.clock;
    }

    //--------------------------------------------------------------------------
    //
    // Element access (maps)
    //
    //--------------------------------------------------------------------------

    template <
        class K,
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type&
    at (K const& k);

    template <
        class K,
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type const&
    at (K const& k) const;

    template <
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type&
    operator[] (Key const& key);

    template <
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if <maybe_map && ! maybe_multi>::type>
    typename std::conditional <IsMap, T, void*>::type&
    operator[] (Key&& key);

    //--------------------------------------------------------------------------
    //
    // Iterators
    //
    //--------------------------------------------------------------------------

    iterator
    begin ()
    {
        return iterator (m_cont.begin());
    }

    const_iterator
    begin () const
    {
        return const_iterator (m_cont.begin ());
    }

    const_iterator
    cbegin() const
    {
        return const_iterator (m_cont.begin ());
    }

    iterator
    end ()
    {
        return iterator (m_cont.end ());
    }

    const_iterator
    end () const
    {
        return const_iterator (m_cont.end ());
    }

    const_iterator
    cend () const
    {
        return const_iterator (m_cont.end ());
    }

    iterator
    iterator_to (value_type& value)
    {
        static_assert (std::is_standard_layout <element>::value,
            "must be standard layout");
        return m_cont.iterator_to (*reinterpret_cast <element*>(
             reinterpret_cast<uint8_t*>(&value)-((std::size_t)
                std::addressof(((element*)0)->member))));
    }

    const_iterator
    iterator_to (value_type const& value) const
    {
        static_assert (std::is_standard_layout <element>::value,
            "must be standard layout");
        return m_cont.iterator_to (*reinterpret_cast <element const*>(
             reinterpret_cast<uint8_t const*>(&value)-((std::size_t)
                std::addressof(((element*)0)->member))));
    }

    //--------------------------------------------------------------------------
    //
    // Capacity
    //
    //--------------------------------------------------------------------------

    bool empty() const noexcept
    {
        return m_cont.empty();
    }

    size_type size() const noexcept
    {
        return m_cont.size();
    }

    size_type max_size() const noexcept
    {
        return m_config.max_size();
    }

    //--------------------------------------------------------------------------
    //
    // Modifiers
    //
    //--------------------------------------------------------------------------

    void clear();

    // map, set
    template <bool maybe_multi = IsMulti>
    auto
    insert (value_type const& value) ->
        typename std::enable_if <! maybe_multi,
            std::pair <iterator, bool>>::type;

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    auto
    insert (value_type const& value) ->
        typename std::enable_if <maybe_multi,
            iterator>::type;

    // map, set
    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    auto
    insert (value_type&& value) ->
        typename std::enable_if <! maybe_multi && ! maybe_map,
            std::pair <iterator, bool>>::type;

    // multimap, multiset
    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    auto
    insert (value_type&& value) ->
        typename std::enable_if <maybe_multi && ! maybe_map,
            iterator>::type;

    // map, set
    template <bool maybe_multi = IsMulti>
    typename std::enable_if <! maybe_multi,
        iterator>::type
    insert (const_iterator /*hint*/, value_type const& value)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert (value).first;
    }

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    typename std::enable_if <maybe_multi,
        iterator>::type
    insert (const_iterator /*hint*/, value_type const& value)
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert (value);
    }

    // map, set
    template <bool maybe_multi = IsMulti>
    typename std::enable_if <! maybe_multi,
        iterator>::type
    insert (const_iterator /*hint*/, value_type&& value)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert (std::move (value)).first;
    }

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    typename std::enable_if <maybe_multi,
        iterator>::type
    insert (const_iterator /*hint*/, value_type&& value)
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert (std::move (value));
    }

    // map, multimap
    template <
        class P,
        bool maybe_map = IsMap
    >
    typename std::enable_if <maybe_map &&
        std::is_constructible <value_type, P&&>::value,
        typename std::conditional <IsMulti,
            iterator, std::pair <iterator, bool>
        >::type
    >::type
    insert (P&& value)
    {
        return emplace (std::forward <P> (value));
    }

    // map, multimap
    template <
        class P,
        bool maybe_map = IsMap
    >
    typename std::enable_if <maybe_map &&
        std::is_constructible <value_type, P&&>::value,
        typename std::conditional <IsMulti,
            iterator, std::pair <iterator, bool>
        >::type
    >::type
    insert (const_iterator hint, P&& value)
    {
        return emplace_hint (hint, std::forward <P> (value));
    }

    template <class InputIt>
    void insert (InputIt first, InputIt last)
    {
        insert (first, last,
            typename std::iterator_traits <
                InputIt>::iterator_category());
    }

    void
    insert (std::initializer_list <value_type> init)
    {
        insert (init.begin(), init.end());
    }

    // set, map
    template <bool maybe_multi = IsMulti, class... Args>
    auto
    emplace (Args&&... args) ->
        typename std::enable_if <! maybe_multi,
            std::pair <iterator, bool>>::type;

    // multiset, multimap
    template <bool maybe_multi = IsMulti, class... Args>
    auto
    emplace (Args&&... args) ->
        typename std::enable_if <maybe_multi,
            iterator>::type;

    // set, map
    template <bool maybe_multi = IsMulti, class... Args>
    auto
    emplace_hint (const_iterator /*hint*/, Args&&... args) ->
        typename std::enable_if <! maybe_multi,
            std::pair <iterator, bool>>::type;

    // multiset, multimap
    template <bool maybe_multi = IsMulti, class... Args>
    typename std::enable_if <maybe_multi,
        iterator>::type
    emplace_hint (const_iterator /*hint*/, Args&&... args)
    {
        // VFALCO TODO The hint could be used for multi, to let
        //             the client order equal ranges
        return emplace <maybe_multi> (
            std::forward <Args> (args)...);
    }

    template <bool is_const, class Iterator, class Base>
    beast::detail::aged_container_iterator <false, Iterator, Base>
    erase (beast::detail::aged_container_iterator <
        is_const, Iterator, Base> pos);

    template <bool is_const, class Iterator, class Base>
    beast::detail::aged_container_iterator <false, Iterator, Base>
    erase (beast::detail::aged_container_iterator <
        is_const, Iterator, Base> first,
            beast::detail::aged_container_iterator <
                is_const, Iterator, Base> last);

    template <class K>
    auto
    erase (K const& k) ->
        size_type;

    void
    swap (aged_unordered_container& other) noexcept;

    template <bool is_const, class Iterator, class Base>
    void
    touch (beast::detail::aged_container_iterator <
        is_const, Iterator, Base> pos)
    {
        touch (pos, clock().now());
    }

    template <class K>
    auto
    touch (K const& k) ->
        size_type;

    //--------------------------------------------------------------------------
    //
    // Lookup
    //
    //--------------------------------------------------------------------------

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    size_type
    count (K const& k) const
    {
        return m_cont.count (k, std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    find (K const& k)
    {
        return iterator (m_cont.find (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    find (K const& k) const
    {
        return const_iterator (m_cont.find (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair <iterator, iterator>
    equal_range (K const& k)
    {
        auto const r (m_cont.equal_range (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
        return std::make_pair (iterator (r.first),
            iterator (r.second));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair <const_iterator, const_iterator>
    equal_range (K const& k) const
    {
        auto const r (m_cont.equal_range (k,
            std::cref (m_config.hash_function()),
                std::cref (m_config.key_value_equal())));
        return std::make_pair (const_iterator (r.first),
            const_iterator (r.second));
    }

    //--------------------------------------------------------------------------
    //
    // Bucket interface
    //
    //--------------------------------------------------------------------------

    local_iterator begin (size_type n)
    {
        return local_iterator (m_cont.begin (n));
    }

    const_local_iterator begin (size_type n) const
    {
        return const_local_iterator (m_cont.begin (n));
    }

    const_local_iterator cbegin (size_type n) const
    {
        return const_local_iterator (m_cont.begin (n));
    }

    local_iterator end (size_type n)
    {
        return local_iterator (m_cont.end (n));
    }

    const_local_iterator end (size_type n) const
    {
        return const_local_iterator (m_cont.end (n));
    }

    const_local_iterator cend (size_type n) const
    {
        return const_local_iterator (m_cont.end (n));
    }

    size_type bucket_count() const
    {
        return m_cont.bucket_count();
    }

    size_type max_bucket_count() const
    {
        return m_buck.max_bucket_count();
    }

    size_type bucket_size (size_type n) const
    {
        return m_cont.bucket_size (n);
    }

    size_type bucket (Key const& k) const
    {
        assert (bucket_count() != 0);
        return m_cont.bucket (k,
            std::cref (m_config.hash_function()));
    }

    //--------------------------------------------------------------------------
    //
    // Hash policy
    //
    //--------------------------------------------------------------------------

    float load_factor() const
    {
        return size() /
            static_cast <float> (m_cont.bucket_count());
    }

    float max_load_factor() const
    {
        return m_buck.max_load_factor();
    }

    void max_load_factor (float ml)
    {
        m_buck.max_load_factor () =
            std::max (ml, m_buck.max_load_factor());
    }

    void rehash (size_type count)
    {
        count = std::max (count,
            size_type (size() / max_load_factor()));
        m_buck.rehash (count, m_cont);
    }

    void reserve (size_type count)
    {
        rehash (std::ceil (count / max_load_factor()));
    }

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    hasher const& hash_function() const
    {
        return m_config.hash_function();
    }

    key_equal const& key_eq () const
    {
        return m_config.key_eq();
    }

    //--------------------------------------------------------------------------
    //
    // Comparison
    //
    //--------------------------------------------------------------------------

    // This differs from the standard in that the comparison
    // is only done on the key portion of the value type, ignoring
    // the mapped type.
    //
    template <
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherAllocator,
        bool maybe_multi = IsMulti
    >
    typename std::enable_if <! maybe_multi, bool>::type
    operator== (
        aged_unordered_container <false, OtherIsMap,
            OtherKey, OtherT, OtherDuration, OtherHash, KeyEqual,
                OtherAllocator> const& other) const;

    template <
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherAllocator,
        bool maybe_multi = IsMulti
    >
    typename std::enable_if <maybe_multi, bool>::type
    operator== (
        aged_unordered_container <true, OtherIsMap,
            OtherKey, OtherT, OtherDuration, OtherHash, KeyEqual,
                OtherAllocator> const& other) const;

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherAllocator
    >
    bool operator!= (
        aged_unordered_container <OtherIsMulti, OtherIsMap,
            OtherKey, OtherT, OtherDuration, OtherHash, KeyEqual,
                OtherAllocator> const& other) const
    {
        return ! (this->operator== (other));
    }

private:
    bool
    would_exceed (size_type additional) const
    {
        return size() + additional >
            bucket_count() * max_load_factor();
    }

    void
    maybe_rehash (size_type additional)
    {
        if (would_exceed (additional))
            m_buck.resize (size() + additional, m_cont);
        assert (load_factor() <= max_load_factor());
    }

    // map, set
    template <bool maybe_multi = IsMulti>
    auto
    insert_unchecked (value_type const& value) ->
        typename std::enable_if <! maybe_multi,
            std::pair <iterator, bool>>::type;

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    auto
    insert_unchecked (value_type const& value) ->
        typename std::enable_if <maybe_multi,
            iterator>::type;

    template <class InputIt>
    void
    insert_unchecked (InputIt first, InputIt last)
    {
        for (; first != last; ++first)
            insert_unchecked (*first);
    }

    template <class InputIt>
    void
    insert (InputIt first, InputIt last,
        std::input_iterator_tag)
    {
        for (; first != last; ++first)
            insert (*first);
    }

    template <class InputIt>
    void
    insert (InputIt first, InputIt last,
        std::random_access_iterator_tag)
    {
        auto const n (std::distance (first, last));
        maybe_rehash (n);
        insert_unchecked (first, last);
    }

    template <bool is_const, class Iterator, class Base>
    void
    touch (beast::detail::aged_container_iterator <
        is_const, Iterator, Base> pos,
            typename clock_type::time_point const& now)
    {
        auto& e (*pos.iterator());
        e.when = now;
        chronological.list.erase (chronological.list.iterator_to (e));
        chronological.list.push_back (e);
    }

    template <bool maybe_propagate = std::allocator_traits <
        Allocator>::propagate_on_container_swap::value>
    typename std::enable_if <maybe_propagate>::type
    swap_data (aged_unordered_container& other) noexcept
    {
        std::swap (m_config.key_compare(), other.m_config.key_compare());
        std::swap (m_config.alloc(), other.m_config.alloc());
        std::swap (m_config.clock, other.m_config.clock);
    }

    template <bool maybe_propagate = std::allocator_traits <
        Allocator>::propagate_on_container_swap::value>
    typename std::enable_if <! maybe_propagate>::type
    swap_data (aged_unordered_container& other) noexcept
    {
        std::swap (m_config.key_compare(), other.m_config.key_compare());
        std::swap (m_config.clock, other.m_config.clock);
    }

private:
    config_t m_config;
    Buckets m_buck;
    cont_type mutable m_cont;
};

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock)
    : m_config (clock)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock,
    Hash const& hash)
    : m_config (clock, hash)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock,
    KeyEqual const& key_eq)
    : m_config (clock, key_eq)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock,
    Allocator const& alloc)
    : m_config (clock, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock,
    Hash const& hash,
    KeyEqual const& key_eq)
    : m_config (clock, hash, key_eq)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock,
    Hash const& hash,
    Allocator const& alloc)
    : m_config (clock, hash, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock,
    KeyEqual const& key_eq,
    Allocator const& alloc)
    : m_config (clock, key_eq, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (
    clock_type& clock,
    Hash const& hash,
    KeyEqual const& key_eq,
    Allocator const& alloc)
    : m_config (clock, hash, key_eq, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock)
    : m_config (clock)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock,
    Hash const& hash)
    : m_config (clock, hash)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock,
    KeyEqual const& key_eq)
    : m_config (clock, key_eq)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock,
    Allocator const& alloc)
    : m_config (clock, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock,
    Hash const& hash,
    KeyEqual const& key_eq)
    : m_config (clock, hash, key_eq)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock,
    Hash const& hash,
    Allocator const& alloc)
    : m_config (clock, hash, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock,
    KeyEqual const& key_eq,
    Allocator const& alloc)
    : m_config (clock, key_eq, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class InputIt>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (InputIt first, InputIt last,
    clock_type& clock,
    Hash const& hash,
    KeyEqual const& key_eq,
    Allocator const& alloc)
    : m_config (clock, hash, key_eq, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (aged_unordered_container const& other)
    : m_config (other.m_config)
    , m_buck (m_config.alloc())
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (other.cbegin(), other.cend());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (aged_unordered_container const& other,
    Allocator const& alloc)
    : m_config (other.m_config, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (other.cbegin(), other.cend());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (aged_unordered_container&& other)
    : m_config (std::move (other.m_config))
    , m_buck (std::move (other.m_buck))
    , m_cont (std::move (other.m_cont))
{
    chronological.list = std::move (other.chronological.list);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (aged_unordered_container&& other,
    Allocator const& alloc)
    : m_config (std::move (other.m_config), alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (other.cbegin(), other.cend());
    other.clear ();
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock)
    : m_config (clock)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock,
    Hash const& hash)
    : m_config (clock, hash)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock,
    KeyEqual const& key_eq)
    : m_config (clock, key_eq)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock,
    Allocator const& alloc)
    : m_config (clock, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock,
    Hash const& hash,
    KeyEqual const& key_eq)
    : m_config (clock, hash, key_eq)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock,
    Hash const& hash,
    Allocator const& alloc)
    : m_config (clock, hash, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock,
    KeyEqual const& key_eq,
    Allocator const& alloc)
    : m_config (clock, key_eq, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
aged_unordered_container (std::initializer_list <value_type> init,
    clock_type& clock,
    Hash const& hash,
    KeyEqual const& key_eq,
    Allocator const& alloc)
    : m_config (clock, hash, key_eq, alloc)
    , m_buck (alloc)
    , m_cont (m_buck,
        std::cref (m_config.value_hash()),
            std::cref (m_config.key_value_equal()))
{
    insert (init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
~aged_unordered_container()
{
    clear();
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
operator= (aged_unordered_container const& other)
    -> aged_unordered_container&
{
    if (this != &other)
    {
        size_type const n (other.size());
        clear();
        m_config = other.m_config;
        m_buck = Buckets (m_config.alloc());
        maybe_rehash (n);
        insert_unchecked (other.begin(), other.end());
    }
    return *this;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
operator= (aged_unordered_container&& other) ->
    aged_unordered_container&
{
    size_type const n (other.size());
    clear();
    m_config = std::move (other.m_config);
    m_buck = Buckets (m_config.alloc());
    maybe_rehash (n);
    insert_unchecked (other.begin(), other.end());
    other.clear();
    return *this;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
operator= (std::initializer_list <value_type> init) ->
    aged_unordered_container&
{
    clear ();
    insert (init);
    return *this;
}

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class K, bool maybe_multi, bool maybe_map, class>
typename std::conditional <IsMap, T, void*>::type&
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
at (K const& k)
{
    auto const iter (m_cont.find (k,
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal())));
    if (iter == m_cont.end())
        throw std::out_of_range ("key not found");
    return iter->value.second;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class K, bool maybe_multi, bool maybe_map, class>
typename std::conditional <IsMap, T, void*>::type const&
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
at (K const& k) const
{
    auto const iter (m_cont.find (k,
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal())));
    if (iter == m_cont.end())
        throw std::out_of_range ("key not found");
    return iter->value.second;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, bool maybe_map, class>
typename std::conditional <IsMap, T, void*>::type&
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
operator[] (Key const& key)
{
    maybe_rehash (1);
    typename cont_type::insert_commit_data d;
    auto const result (m_cont.insert_check (key,
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()), d));
    if (result.second)
    {
        element* const p (new_element (
            std::piecewise_construct,
                std::forward_as_tuple (key),
                    std::forward_as_tuple ()));
        m_cont.insert_commit (*p, d);
        chronological.list.push_back (*p);
        return p->value.second;
    }
    return result.first->value.second;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, bool maybe_map, class>
typename std::conditional <IsMap, T, void*>::type&
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
operator[] (Key&& key)
{
    maybe_rehash (1);
    typename cont_type::insert_commit_data d;
    auto const result (m_cont.insert_check (key,
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()), d));
    if (result.second)
    {
        element* const p (new_element (
            std::piecewise_construct,
                std::forward_as_tuple (std::move (key)),
                    std::forward_as_tuple ()));
        m_cont.insert_commit (*p, d);
        chronological.list.push_back (*p);
        return p->value.second;
    }
    return result.first->value.second;
}

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
void
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
clear()
{
    for (auto iter (chronological.list.begin());
        iter != chronological.list.end();)
        unlink_and_delete_element (&*iter++);
    chronological.list.clear();
    m_cont.clear();
    m_buck.clear();
}

// map, set
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
insert (value_type const& value) ->
    typename std::enable_if <! maybe_multi,
        std::pair <iterator, bool>>::type
{
    maybe_rehash (1);
    typename cont_type::insert_commit_data d;
    auto const result (m_cont.insert_check (extract (value),
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()), d));
    if (result.second)
    {
        element* const p (new_element (value));
        auto const iter (m_cont.insert_commit (*p, d));
        chronological.list.push_back (*p);
        return std::make_pair (iterator (iter), true);
    }
    return std::make_pair (iterator (result.first), false);
}

// multimap, multiset
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
insert (value_type const& value) ->
    typename std::enable_if <maybe_multi,
        iterator>::type
{
    maybe_rehash (1);
    element* const p (new_element (value));
    chronological.list.push_back (*p);
    auto const iter (m_cont.insert (*p));
    return iterator (iter);
}

// map, set
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, bool maybe_map>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
insert (value_type&& value) ->
    typename std::enable_if <! maybe_multi && ! maybe_map,
        std::pair <iterator, bool>>::type
{
    maybe_rehash (1);
    typename cont_type::insert_commit_data d;
    auto const result (m_cont.insert_check (extract (value),
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()), d));
    if (result.second)
    {
        element* const p (new_element (std::move (value)));
        auto const iter (m_cont.insert_commit (*p, d));
        chronological.list.push_back (*p);
        return std::make_pair (iterator (iter), true);
    }
    return std::make_pair (iterator (result.first), false);
}

// multimap, multiset
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, bool maybe_map>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
insert (value_type&& value) ->
    typename std::enable_if <maybe_multi && ! maybe_map,
        iterator>::type
{
    maybe_rehash (1);
    element* const p (new_element (std::move (value)));
    chronological.list.push_back (*p);
    auto const iter (m_cont.insert (*p));
    return iterator (iter);
}

#if 1 // Use insert() instead of insert_check() insert_commit()
// set, map
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, class... Args>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
emplace (Args&&... args) ->
    typename std::enable_if <! maybe_multi,
        std::pair <iterator, bool>>::type
{
    maybe_rehash (1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    element* const p (new_element (std::forward <Args> (args)...));
    auto const result (m_cont.insert (*p));
    if (result.second)
    {
        chronological.list.push_back (*p);
        return std::make_pair (iterator (result.first), true);
    }
    delete_element (p);
    return std::make_pair (iterator (result.first), false);
}
#else // As original, use insert_check() / insert_commit () pair.
// set, map
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, class... Args>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
emplace (Args&&... args) ->
    typename std::enable_if <! maybe_multi,
        std::pair <iterator, bool>>::type
{
    maybe_rehash (1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    element* const p (new_element (
        std::forward <Args> (args)...));
    typename cont_type::insert_commit_data d;
    auto const result (m_cont.insert_check (extract (p->value),
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()), d));
    if (result.second)
    {
        auto const iter (m_cont.insert_commit (*p, d));
        chronological.list.push_back (*p);
        return std::make_pair (iterator (iter), true);
    }
    delete_element (p);
    return std::make_pair (iterator (result.first), false);
}
#endif // 0

// multiset, multimap
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, class... Args>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
emplace (Args&&... args) ->
    typename std::enable_if <maybe_multi,
        iterator>::type
{
    maybe_rehash (1);
    element* const p (new_element (
        std::forward <Args> (args)...));
    chronological.list.push_back (*p);
    auto const iter (m_cont.insert (*p));
    return iterator (iter);
}

// set, map
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi, class... Args>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
emplace_hint (const_iterator /*hint*/, Args&&... args) ->
    typename std::enable_if <! maybe_multi,
        std::pair <iterator, bool>>::type
{
    maybe_rehash (1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    element* const p (new_element (
        std::forward <Args> (args)...));
    typename cont_type::insert_commit_data d;
    auto const result (m_cont.insert_check (extract (p->value),
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()), d));
    if (result.second)
    {
        auto const iter (m_cont.insert_commit (*p, d));
        chronological.list.push_back (*p);
        return std::make_pair (iterator (iter), true);
    }
    delete_element (p);
    return std::make_pair (iterator (result.first), false);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool is_const, class Iterator, class Base>
beast::detail::aged_container_iterator <false, Iterator, Base>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
erase (beast::detail::aged_container_iterator <
    is_const, Iterator, Base> pos)
{
    unlink_and_delete_element(&*((pos++).iterator()));
    return beast::detail::aged_container_iterator <
        false, Iterator, Base> (pos.iterator());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool is_const, class Iterator, class Base>
beast::detail::aged_container_iterator <false, Iterator, Base>
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
erase (beast::detail::aged_container_iterator <
    is_const, Iterator, Base> first,
        beast::detail::aged_container_iterator <
            is_const, Iterator, Base> last)
{
    for (; first != last;)
        unlink_and_delete_element(&*((first++).iterator()));

    return beast::detail::aged_container_iterator <
        false, Iterator, Base> (first.iterator());
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class K>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
erase (K const& k) ->
    size_type
{
    auto iter (m_cont.find (k, std::cref (m_config.hash_function()),
        std::cref (m_config.key_value_equal())));
    if (iter == m_cont.end())
        return 0;
    size_type n (0);
    for (;;)
    {
        auto p (&*iter++);
        bool const done (
            m_config (*p, extract (iter->value)));
        unlink_and_delete_element (p);
        ++n;
        if (done)
            break;
    }
    return n;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
void
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
swap (aged_unordered_container& other) noexcept
{
    swap_data (other);
    std::swap (chronological, other.chronological);
    std::swap (m_cont, other.m_cont);
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <class K>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
touch (K const& k) ->
    size_type
{
    auto const now (clock().now());
    size_type n (0);
    auto const range (equal_range (k));
    for (auto iter : range)
    {
        touch (iter, now);
        ++n;
    }
    return n;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <
    bool OtherIsMap,
    class OtherKey,
    class OtherT,
    class OtherDuration,
    class OtherHash,
    class OtherAllocator,
    bool maybe_multi
>
typename std::enable_if <! maybe_multi, bool>::type
aged_unordered_container <
    IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
operator== (
    aged_unordered_container <false, OtherIsMap,
        OtherKey, OtherT, OtherDuration, OtherHash, KeyEqual,
            OtherAllocator> const& other) const
{
    if (size() != other.size())
        return false;
    for (auto iter (cbegin()), last (cend()), olast (other.cend());
        iter != last; ++iter)
    {
        auto oiter (other.find (extract (*iter)));
        if (oiter == olast)
            return false;
    }
    return true;
}

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <
    bool OtherIsMap,
    class OtherKey,
    class OtherT,
    class OtherDuration,
    class OtherHash,
    class OtherAllocator,
    bool maybe_multi
>
typename std::enable_if <maybe_multi, bool>::type
aged_unordered_container <
    IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
operator== (
    aged_unordered_container <true, OtherIsMap,
        OtherKey, OtherT, OtherDuration, OtherHash, KeyEqual,
            OtherAllocator> const& other) const
{
    if (size() != other.size())
        return false;
    using EqRng = std::pair <const_iterator, const_iterator>;
    for (auto iter (cbegin()), last (cend()); iter != last;)
    {
        auto const& k (extract (*iter));
        auto const eq (equal_range (k));
        auto const oeq (other.equal_range (k));
#if BEAST_NO_CXX14_IS_PERMUTATION
        if (std::distance (eq.first, eq.second) !=
            std::distance (oeq.first, oeq.second) ||
            ! std::is_permutation (eq.first, eq.second, oeq.first))
            return false;
#else
        if (! std::is_permutation (eq.first,
            eq.second, oeq.first, oeq.second))
            return false;
#endif
        iter = eq.second;
    }
    return true;
}

//------------------------------------------------------------------------------

// map, set
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
insert_unchecked (value_type const& value) ->
    typename std::enable_if <! maybe_multi,
        std::pair <iterator, bool>>::type
{
    typename cont_type::insert_commit_data d;
    auto const result (m_cont.insert_check (extract (value),
        std::cref (m_config.hash_function()),
            std::cref (m_config.key_value_equal()), d));
    if (result.second)
    {
        element* const p (new_element (value));
        auto const iter (m_cont.insert_commit (*p, d));
        chronological.list.push_back (*p);
        return std::make_pair (iterator (iter), true);
    }
    return std::make_pair (iterator (result.first), false);
}

// multimap, multiset
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
template <bool maybe_multi>
auto
aged_unordered_container <IsMulti, IsMap, Key, T, Clock,
    Hash, KeyEqual, Allocator>::
insert_unchecked (value_type const& value) ->
    typename std::enable_if <maybe_multi,
        iterator>::type
{
    element* const p (new_element (value));
    chronological.list.push_back (*p);
    auto const iter (m_cont.insert (*p));
    return iterator (iter);
}

//------------------------------------------------------------------------------

}

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator>
struct is_aged_container <beast::detail::aged_unordered_container <
        IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>>
    : std::true_type
{
    explicit is_aged_container() = default;
};

// Free functions

template <bool IsMulti, bool IsMap, class Key, class T, class Clock,
    class Hash, class KeyEqual, class Allocator>
void swap (
    beast::detail::aged_unordered_container <IsMulti, IsMap,
        Key, T, Clock, Hash, KeyEqual, Allocator>& lhs,
    beast::detail::aged_unordered_container <IsMulti, IsMap,
        Key, T, Clock, Hash, KeyEqual, Allocator>& rhs) noexcept
{
    lhs.swap (rhs);
}

/** Expire aged container items past the specified age. */
template <bool IsMulti, bool IsMap, class Key, class T,
    class Clock, class Hash, class KeyEqual, class Allocator,
        class Rep, class Period>
std::size_t expire (beast::detail::aged_unordered_container <
    IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>& c,
        std::chrono::duration <Rep, Period> const& age) noexcept
{
    std::size_t n (0);
    auto const expired (c.clock().now() - age);
    for (auto iter (c.chronological.cbegin());
        iter != c.chronological.cend() &&
            iter.when() <= expired;)
    {
        iter = c.erase (iter);
        ++n;
    }
    return n;
}

}

#endif
