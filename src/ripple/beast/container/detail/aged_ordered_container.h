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

#ifndef BEAST_CONTAINER_DETAIL_AGED_ORDERED_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_DETAIL_AGED_ORDERED_CONTAINER_H_INCLUDED

#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/container/aged_container.h>
#include <ripple/beast/container/detail/aged_associative_container.h>
#include <ripple/beast/container/detail/aged_container_iterator.h>
#include <ripple/beast/container/detail/empty_base_optimization.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/version.hpp>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace beast {
namespace detail {

// Traits templates used to discern reverse_iterators, which are disallowed
// for mutating operations.
template <class It>
struct is_boost_reverse_iterator : std::false_type
{
    explicit is_boost_reverse_iterator() = default;
};

template <class It>
struct is_boost_reverse_iterator<boost::intrusive::reverse_iterator<It>>
    : std::true_type
{
    explicit is_boost_reverse_iterator() = default;
};

/** Associative container where each element is also indexed by time.

    This container mirrors the interface of the standard library ordered
    associative containers, with the addition that each element is associated
    with a `when` `time_point` which is obtained from the value of the clock's
    `now`. The function `touch` updates the time for an element to the current
    time as reported by the clock.

    An extra set of iterator types and member functions are provided in the
    `chronological` memberspace that allow traversal in temporal or reverse
    temporal order. This container is useful as a building block for caches
    whose items expire after a certain amount of time. The chronological
    iterators allow for fully customizable expiration strategies.

    @see aged_set, aged_multiset, aged_map, aged_multimap
*/
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock = std::chrono::steady_clock,
    class Compare = std::less<Key>,
    class Allocator = std::allocator<
        typename std::conditional<IsMap, std::pair<Key const, T>, Key>::type>>
class aged_ordered_container
{
public:
    using clock_type = abstract_clock<Clock>;
    using time_point = typename clock_type::time_point;
    using duration = typename clock_type::duration;
    using key_type = Key;
    using mapped_type = T;
    using value_type =
        typename std::conditional<IsMap, std::pair<Key const, T>, Key>::type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    // Introspection (for unit tests)
    using is_unordered = std::false_type;
    using is_multi = std::integral_constant<bool, IsMulti>;
    using is_map = std::integral_constant<bool, IsMap>;

private:
    static Key const&
    extract(value_type const& value)
    {
        return aged_associative_container_extract_t<IsMap>()(value);
    }

    // VFALCO TODO hoist to remove template argument dependencies
    struct element
        : boost::intrusive::set_base_hook<
              boost::intrusive::link_mode<boost::intrusive::normal_link>>,
          boost::intrusive::list_base_hook<
              boost::intrusive::link_mode<boost::intrusive::normal_link>>
    {
        // Stash types here so the iterator doesn't
        // need to see the container declaration.
        struct stashed
        {
            explicit stashed() = default;

            using value_type = typename aged_ordered_container::value_type;
            using time_point = typename aged_ordered_container::time_point;
        };

        element(time_point const& when_, value_type const& value_)
            : value(value_), when(when_)
        {
        }

        element(time_point const& when_, value_type&& value_)
            : value(std::move(value_)), when(when_)
        {
        }

        template <
            class... Args,
            class = typename std::enable_if<
                std::is_constructible<value_type, Args...>::value>::type>
        element(time_point const& when_, Args&&... args)
            : value(std::forward<Args>(args)...), when(when_)
        {
        }

        value_type value;
        time_point when;
    };

    // VFALCO TODO This should only be enabled for maps.
    class pair_value_compare : public Compare
    {
    public:
        using first_argument = value_type;
        using second_argument = value_type;
        using result_type = bool;

        bool
        operator()(value_type const& lhs, value_type const& rhs) const
        {
            return Compare::operator()(lhs.first, rhs.first);
        }

        pair_value_compare()
        {
        }

        pair_value_compare(pair_value_compare const& other) : Compare(other)
        {
        }

    private:
        friend aged_ordered_container;

        pair_value_compare(Compare const& compare) : Compare(compare)
        {
        }
    };

    // Compares value_type against element, used in insert_check
    // VFALCO TODO hoist to remove template argument dependencies
    class KeyValueCompare : public Compare
    {
    public:
        using first_argument = Key;
        using second_argument = element;
        using result_type = bool;

        KeyValueCompare() = default;

        KeyValueCompare(Compare const& compare) : Compare(compare)
        {
        }

        bool
        operator()(Key const& k, element const& e) const
        {
            return Compare::operator()(k, extract(e.value));
        }

        bool
        operator()(element const& e, Key const& k) const
        {
            return Compare::operator()(extract(e.value), k);
        }

        bool
        operator()(element const& x, element const& y) const
        {
            return Compare::operator()(extract(x.value), extract(y.value));
        }

        Compare&
        compare()
        {
            return *this;
        }

        Compare const&
        compare() const
        {
            return *this;
        }
    };

    using list_type = typename boost::intrusive::
        make_list<element, boost::intrusive::constant_time_size<false>>::type;

    using cont_type = typename std::conditional<
        IsMulti,
        typename boost::intrusive::make_multiset<
            element,
            boost::intrusive::constant_time_size<true>,
            boost::intrusive::compare<KeyValueCompare>>::type,
        typename boost::intrusive::make_set<
            element,
            boost::intrusive::constant_time_size<true>,
            boost::intrusive::compare<KeyValueCompare>>::type>::type;

    using ElementAllocator = typename std::allocator_traits<
        Allocator>::template rebind_alloc<element>;

    using ElementAllocatorTraits = std::allocator_traits<ElementAllocator>;

    class config_t
        : private KeyValueCompare,
          public beast::detail::empty_base_optimization<ElementAllocator>
    {
    public:
        explicit config_t(clock_type& clock_) : clock(clock_)
        {
        }

        config_t(clock_type& clock_, Compare const& comp)
            : KeyValueCompare(comp), clock(clock_)
        {
        }

        config_t(clock_type& clock_, Allocator const& alloc_)
            : beast::detail::empty_base_optimization<ElementAllocator>(alloc_)
            , clock(clock_)
        {
        }

        config_t(
            clock_type& clock_,
            Compare const& comp,
            Allocator const& alloc_)
            : KeyValueCompare(comp)
            , beast::detail::empty_base_optimization<ElementAllocator>(alloc_)
            , clock(clock_)
        {
        }

        config_t(config_t const& other)
            : KeyValueCompare(other.key_compare())
            , beast::detail::empty_base_optimization<ElementAllocator>(
                  ElementAllocatorTraits::select_on_container_copy_construction(
                      other.alloc()))
            , clock(other.clock)
        {
        }

        config_t(config_t const& other, Allocator const& alloc)
            : KeyValueCompare(other.key_compare())
            , beast::detail::empty_base_optimization<ElementAllocator>(alloc)
            , clock(other.clock)
        {
        }

        config_t(config_t&& other)
            : KeyValueCompare(std::move(other.key_compare()))
            , beast::detail::empty_base_optimization<ElementAllocator>(
                  std::move(other))
            , clock(other.clock)
        {
        }

        config_t(config_t&& other, Allocator const& alloc)
            : KeyValueCompare(std::move(other.key_compare()))
            , beast::detail::empty_base_optimization<ElementAllocator>(alloc)
            , clock(other.clock)
        {
        }

        config_t&
        operator=(config_t const& other)
        {
            if (this != &other)
            {
                compare() = other.compare();
                alloc() = other.alloc();
                clock = other.clock;
            }
            return *this;
        }

        config_t&
        operator=(config_t&& other)
        {
            compare() = std::move(other.compare());
            alloc() = std::move(other.alloc());
            clock = other.clock;
            return *this;
        }

        Compare&
        compare()
        {
            return KeyValueCompare::compare();
        }

        Compare const&
        compare() const
        {
            return KeyValueCompare::compare();
        }

        KeyValueCompare&
        key_compare()
        {
            return *this;
        }

        KeyValueCompare const&
        key_compare() const
        {
            return *this;
        }

        ElementAllocator&
        alloc()
        {
            return beast::detail::empty_base_optimization<
                ElementAllocator>::member();
        }

        ElementAllocator const&
        alloc() const
        {
            return beast::detail::empty_base_optimization<
                ElementAllocator>::member();
        }

        std::reference_wrapper<clock_type> clock;
    };

    template <class... Args>
    element*
    new_element(Args&&... args)
    {
        struct Deleter
        {
            std::reference_wrapper<ElementAllocator> a_;
            Deleter(ElementAllocator& a) : a_(a)
            {
            }

            void
            operator()(element* p)
            {
                ElementAllocatorTraits::deallocate(a_.get(), p, 1);
            }
        };

        std::unique_ptr<element, Deleter> p(
            ElementAllocatorTraits::allocate(m_config.alloc(), 1),
            Deleter(m_config.alloc()));
        ElementAllocatorTraits::construct(
            m_config.alloc(),
            p.get(),
            clock().now(),
            std::forward<Args>(args)...);
        return p.release();
    }

    void
    delete_element(element const* p)
    {
        ElementAllocatorTraits::destroy(m_config.alloc(), p);
        ElementAllocatorTraits::deallocate(
            m_config.alloc(), const_cast<element*>(p), 1);
    }

    void
    unlink_and_delete_element(element const* p)
    {
        chronological.list.erase(chronological.list.iterator_to(*p));
        m_cont.erase(m_cont.iterator_to(*p));
        delete_element(p);
    }

public:
    using key_compare = Compare;
    using value_compare =
        typename std::conditional<IsMap, pair_value_compare, Compare>::type;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer =
        typename std::allocator_traits<Allocator>::const_pointer;

    // A set iterator (IsMap==false) is always const
    // because the elements of a set are immutable.
    using iterator = beast::detail::
        aged_container_iterator<!IsMap, typename cont_type::iterator>;
    using const_iterator = beast::detail::
        aged_container_iterator<true, typename cont_type::iterator>;
    using reverse_iterator = beast::detail::
        aged_container_iterator<!IsMap, typename cont_type::reverse_iterator>;
    using const_reverse_iterator = beast::detail::
        aged_container_iterator<true, typename cont_type::reverse_iterator>;

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
        using iterator = beast::detail::
            aged_container_iterator<!IsMap, typename list_type::iterator>;
        using const_iterator = beast::detail::
            aged_container_iterator<true, typename list_type::iterator>;
        using reverse_iterator = beast::detail::aged_container_iterator<
            !IsMap,
            typename list_type::reverse_iterator>;
        using const_reverse_iterator = beast::detail::
            aged_container_iterator<true, typename list_type::reverse_iterator>;

        iterator
        begin()
        {
            return iterator(list.begin());
        }

        const_iterator
        begin() const
        {
            return const_iterator(list.begin());
        }

        const_iterator
        cbegin() const
        {
            return const_iterator(list.begin());
        }

        iterator
        end()
        {
            return iterator(list.end());
        }

        const_iterator
        end() const
        {
            return const_iterator(list.end());
        }

        const_iterator
        cend() const
        {
            return const_iterator(list.end());
        }

        reverse_iterator
        rbegin()
        {
            return reverse_iterator(list.rbegin());
        }

        const_reverse_iterator
        rbegin() const
        {
            return const_reverse_iterator(list.rbegin());
        }

        const_reverse_iterator
        crbegin() const
        {
            return const_reverse_iterator(list.rbegin());
        }

        reverse_iterator
        rend()
        {
            return reverse_iterator(list.rend());
        }

        const_reverse_iterator
        rend() const
        {
            return const_reverse_iterator(list.rend());
        }

        const_reverse_iterator
        crend() const
        {
            return const_reverse_iterator(list.rend());
        }

        iterator
        iterator_to(value_type& value)
        {
            static_assert(
                std::is_standard_layout<element>::value,
                "must be standard layout");
            return list.iterator_to(*reinterpret_cast<element*>(
                reinterpret_cast<uint8_t*>(&value) -
                ((std::size_t)std::addressof(((element*)0)->member))));
        }

        const_iterator
        iterator_to(value_type const& value) const
        {
            static_assert(
                std::is_standard_layout<element>::value,
                "must be standard layout");
            return list.iterator_to(*reinterpret_cast<element const*>(
                reinterpret_cast<uint8_t const*>(&value) -
                ((std::size_t)std::addressof(((element*)0)->member))));
        }

    private:
        chronological_t()
        {
        }

        chronological_t(chronological_t const&) = delete;
        chronological_t(chronological_t&&) = delete;

        friend class aged_ordered_container;
        list_type mutable list;
    } chronological;

    //--------------------------------------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------------------------------------

    aged_ordered_container() = delete;

    explicit aged_ordered_container(clock_type& clock);

    aged_ordered_container(clock_type& clock, Compare const& comp);

    aged_ordered_container(clock_type& clock, Allocator const& alloc);

    aged_ordered_container(
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc);

    template <class InputIt>
    aged_ordered_container(InputIt first, InputIt last, clock_type& clock);

    template <class InputIt>
    aged_ordered_container(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Compare const& comp);

    template <class InputIt>
    aged_ordered_container(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Allocator const& alloc);

    template <class InputIt>
    aged_ordered_container(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc);

    aged_ordered_container(aged_ordered_container const& other);

    aged_ordered_container(
        aged_ordered_container const& other,
        Allocator const& alloc);

    aged_ordered_container(aged_ordered_container&& other);

    aged_ordered_container(
        aged_ordered_container&& other,
        Allocator const& alloc);

    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock);

    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Compare const& comp);

    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Allocator const& alloc);

    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc);

    ~aged_ordered_container();

    aged_ordered_container&
    operator=(aged_ordered_container const& other);

    aged_ordered_container&
    operator=(aged_ordered_container&& other);

    aged_ordered_container&
    operator=(std::initializer_list<value_type> init);

    allocator_type
    get_allocator() const
    {
        return m_config.alloc();
    }

    clock_type&
    clock()
    {
        return m_config.clock;
    }

    clock_type const&
    clock() const
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
        class = typename std::enable_if<maybe_map && !maybe_multi>::type>
    typename std::conditional<IsMap, T, void*>::type&
    at(K const& k);

    template <
        class K,
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if<maybe_map && !maybe_multi>::type>
    typename std::conditional<IsMap, T, void*>::type const&
    at(K const& k) const;

    template <
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if<maybe_map && !maybe_multi>::type>
    typename std::conditional<IsMap, T, void*>::type&
    operator[](Key const& key);

    template <
        bool maybe_multi = IsMulti,
        bool maybe_map = IsMap,
        class = typename std::enable_if<maybe_map && !maybe_multi>::type>
    typename std::conditional<IsMap, T, void*>::type&
    operator[](Key&& key);

    //--------------------------------------------------------------------------
    //
    // Iterators
    //
    //--------------------------------------------------------------------------

    iterator
    begin()
    {
        return iterator(m_cont.begin());
    }

    const_iterator
    begin() const
    {
        return const_iterator(m_cont.begin());
    }

    const_iterator
    cbegin() const
    {
        return const_iterator(m_cont.begin());
    }

    iterator
    end()
    {
        return iterator(m_cont.end());
    }

    const_iterator
    end() const
    {
        return const_iterator(m_cont.end());
    }

    const_iterator
    cend() const
    {
        return const_iterator(m_cont.end());
    }

    reverse_iterator
    rbegin()
    {
        return reverse_iterator(m_cont.rbegin());
    }

    const_reverse_iterator
    rbegin() const
    {
        return const_reverse_iterator(m_cont.rbegin());
    }

    const_reverse_iterator
    crbegin() const
    {
        return const_reverse_iterator(m_cont.rbegin());
    }

    reverse_iterator
    rend()
    {
        return reverse_iterator(m_cont.rend());
    }

    const_reverse_iterator
    rend() const
    {
        return const_reverse_iterator(m_cont.rend());
    }

    const_reverse_iterator
    crend() const
    {
        return const_reverse_iterator(m_cont.rend());
    }

    iterator
    iterator_to(value_type& value)
    {
        static_assert(
            std::is_standard_layout<element>::value, "must be standard layout");
        return m_cont.iterator_to(*reinterpret_cast<element*>(
            reinterpret_cast<uint8_t*>(&value) -
            ((std::size_t)std::addressof(((element*)0)->member))));
    }

    const_iterator
    iterator_to(value_type const& value) const
    {
        static_assert(
            std::is_standard_layout<element>::value, "must be standard layout");
        return m_cont.iterator_to(*reinterpret_cast<element const*>(
            reinterpret_cast<uint8_t const*>(&value) -
            ((std::size_t)std::addressof(((element*)0)->member))));
    }

    //--------------------------------------------------------------------------
    //
    // Capacity
    //
    //--------------------------------------------------------------------------

    bool
    empty() const noexcept
    {
        return m_cont.empty();
    }

    size_type
    size() const noexcept
    {
        return m_cont.size();
    }

    size_type
    max_size() const noexcept
    {
        return m_config.max_size();
    }

    //--------------------------------------------------------------------------
    //
    // Modifiers
    //
    //--------------------------------------------------------------------------

    void
    clear();

    // map, set
    template <bool maybe_multi = IsMulti>
    auto
    insert(value_type const& value) ->
        typename std::enable_if<!maybe_multi, std::pair<iterator, bool>>::type;

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    auto
    insert(value_type const& value) ->
        typename std::enable_if<maybe_multi, iterator>::type;

    // set
    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    auto
    insert(value_type&& value) -> typename std::
        enable_if<!maybe_multi && !maybe_map, std::pair<iterator, bool>>::type;

    // multiset
    template <bool maybe_multi = IsMulti, bool maybe_map = IsMap>
    auto
    insert(value_type&& value) ->
        typename std::enable_if<maybe_multi && !maybe_map, iterator>::type;

    //---

    // map, set
    template <bool maybe_multi = IsMulti>
    auto
    insert(const_iterator hint, value_type const& value) ->
        typename std::enable_if<!maybe_multi, iterator>::type;

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    typename std::enable_if<maybe_multi, iterator>::type
    insert(const_iterator /*hint*/, value_type const& value)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
        return insert(value);
    }

    // map, set
    template <bool maybe_multi = IsMulti>
    auto
    insert(const_iterator hint, value_type&& value) ->
        typename std::enable_if<!maybe_multi, iterator>::type;

    // multimap, multiset
    template <bool maybe_multi = IsMulti>
    typename std::enable_if<maybe_multi, iterator>::type
    insert(const_iterator /*hint*/, value_type&& value)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
        return insert(std::move(value));
    }

    // map, multimap
    template <class P, bool maybe_map = IsMap>
    typename std::enable_if<
        maybe_map && std::is_constructible<value_type, P&&>::value,
        typename std::
            conditional<IsMulti, iterator, std::pair<iterator, bool>>::type>::
        type
        insert(P&& value)
    {
        return emplace(std::forward<P>(value));
    }

    // map, multimap
    template <class P, bool maybe_map = IsMap>
    typename std::enable_if<
        maybe_map && std::is_constructible<value_type, P&&>::value,
        typename std::
            conditional<IsMulti, iterator, std::pair<iterator, bool>>::type>::
        type
        insert(const_iterator hint, P&& value)
    {
        return emplace_hint(hint, std::forward<P>(value));
    }

    template <class InputIt>
    void
    insert(InputIt first, InputIt last)
    {
        for (; first != last; ++first)
            insert(cend(), *first);
    }

    void
    insert(std::initializer_list<value_type> init)
    {
        insert(init.begin(), init.end());
    }

    // map, set
    template <bool maybe_multi = IsMulti, class... Args>
    auto
    emplace(Args&&... args) ->
        typename std::enable_if<!maybe_multi, std::pair<iterator, bool>>::type;

    // multiset, multimap
    template <bool maybe_multi = IsMulti, class... Args>
    auto
    emplace(Args&&... args) ->
        typename std::enable_if<maybe_multi, iterator>::type;

    // map, set
    template <bool maybe_multi = IsMulti, class... Args>
    auto
    emplace_hint(const_iterator hint, Args&&... args) ->
        typename std::enable_if<!maybe_multi, std::pair<iterator, bool>>::type;

    // multiset, multimap
    template <bool maybe_multi = IsMulti, class... Args>
    typename std::enable_if<maybe_multi, iterator>::type
    emplace_hint(const_iterator /*hint*/, Args&&... args)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
        return emplace<maybe_multi>(std::forward<Args>(args)...);
    }

    // enable_if prevents erase (reverse_iterator pos) from compiling
    template <
        bool is_const,
        class Iterator,
        class = std::enable_if_t<!is_boost_reverse_iterator<Iterator>::value>>
    beast::detail::aged_container_iterator<false, Iterator>
    erase(beast::detail::aged_container_iterator<is_const, Iterator> pos);

    // enable_if prevents erase (reverse_iterator first, reverse_iterator last)
    // from compiling
    template <
        bool is_const,
        class Iterator,
        class = std::enable_if_t<!is_boost_reverse_iterator<Iterator>::value>>
    beast::detail::aged_container_iterator<false, Iterator>
    erase(
        beast::detail::aged_container_iterator<is_const, Iterator> first,
        beast::detail::aged_container_iterator<is_const, Iterator> last);

    template <class K>
    auto
    erase(K const& k) -> size_type;

    void
    swap(aged_ordered_container& other) noexcept;

    //--------------------------------------------------------------------------

    // enable_if prevents touch (reverse_iterator pos) from compiling
    template <
        bool is_const,
        class Iterator,
        class = std::enable_if_t<!is_boost_reverse_iterator<Iterator>::value>>
    void
    touch(beast::detail::aged_container_iterator<is_const, Iterator> pos)
    {
        touch(pos, clock().now());
    }

    template <class K>
    size_type
    touch(K const& k);

    //--------------------------------------------------------------------------
    //
    // Lookup
    //
    //--------------------------------------------------------------------------

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    size_type
    count(K const& k) const
    {
        return m_cont.count(k, std::cref(m_config.key_compare()));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    find(K const& k)
    {
        return iterator(m_cont.find(k, std::cref(m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    find(K const& k) const
    {
        return const_iterator(
            m_cont.find(k, std::cref(m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<iterator, iterator>
    equal_range(K const& k)
    {
        auto const r(m_cont.equal_range(k, std::cref(m_config.key_compare())));
        return std::make_pair(iterator(r.first), iterator(r.second));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<const_iterator, const_iterator>
    equal_range(K const& k) const
    {
        auto const r(m_cont.equal_range(k, std::cref(m_config.key_compare())));
        return std::make_pair(
            const_iterator(r.first), const_iterator(r.second));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    lower_bound(K const& k)
    {
        return iterator(
            m_cont.lower_bound(k, std::cref(m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    lower_bound(K const& k) const
    {
        return const_iterator(
            m_cont.lower_bound(k, std::cref(m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    upper_bound(K const& k)
    {
        return iterator(
            m_cont.upper_bound(k, std::cref(m_config.key_compare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    upper_bound(K const& k) const
    {
        return const_iterator(
            m_cont.upper_bound(k, std::cref(m_config.key_compare())));
    }

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    key_compare
    key_comp() const
    {
        return m_config.compare();
    }

    // VFALCO TODO Should this return const reference for set?
    value_compare
    value_comp() const
    {
        return value_compare(m_config.compare());
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
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator>
    bool
    operator==(aged_ordered_container<
               OtherIsMulti,
               OtherIsMap,
               Key,
               OtherT,
               OtherDuration,
               Compare,
               OtherAllocator> const& other) const;

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator>
    bool
    operator!=(aged_ordered_container<
               OtherIsMulti,
               OtherIsMap,
               Key,
               OtherT,
               OtherDuration,
               Compare,
               OtherAllocator> const& other) const
    {
        return !(this->operator==(other));
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator>
    bool
    operator<(aged_ordered_container<
              OtherIsMulti,
              OtherIsMap,
              Key,
              OtherT,
              OtherDuration,
              Compare,
              OtherAllocator> const& other) const
    {
        value_compare const comp(value_comp());
        return std::lexicographical_compare(
            cbegin(), cend(), other.cbegin(), other.cend(), comp);
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator>
    bool
    operator<=(aged_ordered_container<
               OtherIsMulti,
               OtherIsMap,
               Key,
               OtherT,
               OtherDuration,
               Compare,
               OtherAllocator> const& other) const
    {
        return !(other < *this);
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator>
    bool
    operator>(aged_ordered_container<
              OtherIsMulti,
              OtherIsMap,
              Key,
              OtherT,
              OtherDuration,
              Compare,
              OtherAllocator> const& other) const
    {
        return other < *this;
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator>
    bool
    operator>=(aged_ordered_container<
               OtherIsMulti,
               OtherIsMap,
               Key,
               OtherT,
               OtherDuration,
               Compare,
               OtherAllocator> const& other) const
    {
        return !(*this < other);
    }

private:
    // enable_if prevents erase (reverse_iterator pos, now) from compiling
    template <
        bool is_const,
        class Iterator,
        class = std::enable_if_t<!is_boost_reverse_iterator<Iterator>::value>>
    void
    touch(
        beast::detail::aged_container_iterator<is_const, Iterator> pos,
        typename clock_type::time_point const& now);

    template <
        bool maybe_propagate = std::allocator_traits<
            Allocator>::propagate_on_container_swap::value>
    typename std::enable_if<maybe_propagate>::type
    swap_data(aged_ordered_container& other) noexcept;

    template <
        bool maybe_propagate = std::allocator_traits<
            Allocator>::propagate_on_container_swap::value>
    typename std::enable_if<!maybe_propagate>::type
    swap_data(aged_ordered_container& other) noexcept;

private:
    config_t m_config;
    cont_type mutable m_cont;
};

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(clock_type& clock)
    : m_config(clock)
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(clock_type& clock, Compare const& comp)
    : m_config(clock, comp), m_cont(comp)
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(clock_type& clock, Allocator const& alloc)
    : m_config(clock, alloc)
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc)
    : m_config(clock, comp, alloc), m_cont(comp)
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class InputIt>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(InputIt first, InputIt last, clock_type& clock)
    : m_config(clock)
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class InputIt>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Compare const& comp)
    : m_config(clock, comp), m_cont(comp)
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class InputIt>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Allocator const& alloc)
    : m_config(clock, alloc)
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class InputIt>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc)
    : m_config(clock, comp, alloc), m_cont(comp)
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(aged_ordered_container const& other)
    : m_config(other.m_config)
#if BOOST_VERSION >= 108000
    , m_cont(other.m_cont.get_comp())
#else
    , m_cont(other.m_cont.comp())
#endif
{
    insert(other.cbegin(), other.cend());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        aged_ordered_container const& other,
        Allocator const& alloc)
    : m_config(other.m_config, alloc)
#if BOOST_VERSION >= 108000
    , m_cont(other.m_cont.get_comp())
#else
    , m_cont(other.m_cont.comp())
#endif
{
    insert(other.cbegin(), other.cend());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(aged_ordered_container&& other)
    : m_config(std::move(other.m_config)), m_cont(std::move(other.m_cont))
{
    chronological.list = std::move(other.chronological.list);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        aged_ordered_container&& other,
        Allocator const& alloc)
    : m_config(std::move(other.m_config), alloc)
#if BOOST_VERSION >= 108000
    , m_cont(std::move(other.m_cont.get_comp()))
#else
    , m_cont(std::move(other.m_cont.comp()))
#endif

{
    insert(other.cbegin(), other.cend());
    other.clear();
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock)
    : m_config(clock)
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Compare const& comp)
    : m_config(clock, comp), m_cont(comp)
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Allocator const& alloc)
    : m_config(clock, alloc)
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    aged_ordered_container(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc)
    : m_config(clock, comp, alloc), m_cont(comp)
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    ~aged_ordered_container()
{
    clear();
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
operator=(aged_ordered_container const& other) -> aged_ordered_container&
{
    if (this != &other)
    {
        clear();
        this->m_config = other.m_config;
        insert(other.begin(), other.end());
    }
    return *this;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
operator=(aged_ordered_container&& other) -> aged_ordered_container&
{
    clear();
    this->m_config = std::move(other.m_config);
    insert(other.begin(), other.end());
    other.clear();
    return *this;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
operator=(std::initializer_list<value_type> init) -> aged_ordered_container&
{
    clear();
    insert(init);
    return *this;
}

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class K, bool maybe_multi, bool maybe_map, class>
typename std::conditional<IsMap, T, void*>::type&
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::at(
    K const& k)
{
    auto const iter(m_cont.find(k, std::cref(m_config.key_compare())));
    if (iter == m_cont.end())
        throw std::out_of_range("key not found");
    return iter->value.second;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class K, bool maybe_multi, bool maybe_map, class>
typename std::conditional<IsMap, T, void*>::type const&
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::at(
    K const& k) const
{
    auto const iter(m_cont.find(k, std::cref(m_config.key_compare())));
    if (iter == m_cont.end())
        throw std::out_of_range("key not found");
    return iter->value.second;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi, bool maybe_map, class>
typename std::conditional<IsMap, T, void*>::type&
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
operator[](Key const& key)
{
    typename cont_type::insert_commit_data d;
    auto const result(
        m_cont.insert_check(key, std::cref(m_config.key_compare()), d));
    if (result.second)
    {
        element* const p(new_element(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple()));
        m_cont.insert_commit(*p, d);
        chronological.list.push_back(*p);
        return p->value.second;
    }
    return result.first->value.second;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi, bool maybe_map, class>
typename std::conditional<IsMap, T, void*>::type&
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
operator[](Key&& key)
{
    typename cont_type::insert_commit_data d;
    auto const result(
        m_cont.insert_check(key, std::cref(m_config.key_compare()), d));
    if (result.second)
    {
        element* const p(new_element(
            std::piecewise_construct,
            std::forward_as_tuple(std::move(key)),
            std::forward_as_tuple()));
        m_cont.insert_commit(*p, d);
        chronological.list.push_back(*p);
        return p->value.second;
    }
    return result.first->value.second;
}

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
void
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    clear()
{
    for (auto iter(chronological.list.begin());
         iter != chronological.list.end();)
        delete_element(&*iter++);
    chronological.list.clear();
    m_cont.clear();
}

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    insert(value_type const& value) ->
    typename std::enable_if<!maybe_multi, std::pair<iterator, bool>>::type
{
    typename cont_type::insert_commit_data d;
    auto const result(m_cont.insert_check(
        extract(value), std::cref(m_config.key_compare()), d));
    if (result.second)
    {
        element* const p(new_element(value));
        auto const iter(m_cont.insert_commit(*p, d));
        chronological.list.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    return std::make_pair(iterator(result.first), false);
}

// multimap, multiset
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    insert(value_type const& value) ->
    typename std::enable_if<maybe_multi, iterator>::type
{
    auto const before(
        m_cont.upper_bound(extract(value), std::cref(m_config.key_compare())));
    element* const p(new_element(value));
    chronological.list.push_back(*p);
    auto const iter(m_cont.insert_before(before, *p));
    return iterator(iter);
}

// set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi, bool maybe_map>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    insert(value_type&& value) -> typename std::
        enable_if<!maybe_multi && !maybe_map, std::pair<iterator, bool>>::type
{
    typename cont_type::insert_commit_data d;
    auto const result(m_cont.insert_check(
        extract(value), std::cref(m_config.key_compare()), d));
    if (result.second)
    {
        element* const p(new_element(std::move(value)));
        auto const iter(m_cont.insert_commit(*p, d));
        chronological.list.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    return std::make_pair(iterator(result.first), false);
}

// multiset
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi, bool maybe_map>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    insert(value_type&& value) ->
    typename std::enable_if<maybe_multi && !maybe_map, iterator>::type
{
    auto const before(
        m_cont.upper_bound(extract(value), std::cref(m_config.key_compare())));
    element* const p(new_element(std::move(value)));
    chronological.list.push_back(*p);
    auto const iter(m_cont.insert_before(before, *p));
    return iterator(iter);
}

//---

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    insert(const_iterator hint, value_type const& value) ->
    typename std::enable_if<!maybe_multi, iterator>::type
{
    typename cont_type::insert_commit_data d;
    auto const result(m_cont.insert_check(
        hint.iterator(), extract(value), std::cref(m_config.key_compare()), d));
    if (result.second)
    {
        element* const p(new_element(value));
        auto const iter(m_cont.insert_commit(*p, d));
        chronological.list.push_back(*p);
        return iterator(iter);
    }
    return iterator(result.first);
}

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    insert(const_iterator hint, value_type&& value) ->
    typename std::enable_if<!maybe_multi, iterator>::type
{
    typename cont_type::insert_commit_data d;
    auto const result(m_cont.insert_check(
        hint.iterator(), extract(value), std::cref(m_config.key_compare()), d));
    if (result.second)
    {
        element* const p(new_element(std::move(value)));
        auto const iter(m_cont.insert_commit(*p, d));
        chronological.list.push_back(*p);
        return iterator(iter);
    }
    return iterator(result.first);
}

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi, class... Args>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    emplace(Args&&... args) ->
    typename std::enable_if<!maybe_multi, std::pair<iterator, bool>>::type
{
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    element* const p(new_element(std::forward<Args>(args)...));
    typename cont_type::insert_commit_data d;
    auto const result(m_cont.insert_check(
        extract(p->value), std::cref(m_config.key_compare()), d));
    if (result.second)
    {
        auto const iter(m_cont.insert_commit(*p, d));
        chronological.list.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    delete_element(p);
    return std::make_pair(iterator(result.first), false);
}

// multiset, multimap
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi, class... Args>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    emplace(Args&&... args) ->
    typename std::enable_if<maybe_multi, iterator>::type
{
    element* const p(new_element(std::forward<Args>(args)...));
    auto const before(m_cont.upper_bound(
        extract(p->value), std::cref(m_config.key_compare())));
    chronological.list.push_back(*p);
    auto const iter(m_cont.insert_before(before, *p));
    return iterator(iter);
}

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_multi, class... Args>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    emplace_hint(const_iterator hint, Args&&... args) ->
    typename std::enable_if<!maybe_multi, std::pair<iterator, bool>>::type
{
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    element* const p(new_element(std::forward<Args>(args)...));
    typename cont_type::insert_commit_data d;
    auto const result(m_cont.insert_check(
        hint.iterator(),
        extract(p->value),
        std::cref(m_config.key_compare()),
        d));
    if (result.second)
    {
        auto const iter(m_cont.insert_commit(*p, d));
        chronological.list.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    delete_element(p);
    return std::make_pair(iterator(result.first), false);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool is_const, class Iterator, class>
beast::detail::aged_container_iterator<false, Iterator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    erase(beast::detail::aged_container_iterator<is_const, Iterator> pos)
{
    unlink_and_delete_element(&*((pos++).iterator()));
    return beast::detail::aged_container_iterator<false, Iterator>(
        pos.iterator());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool is_const, class Iterator, class>
beast::detail::aged_container_iterator<false, Iterator>
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    erase(
        beast::detail::aged_container_iterator<is_const, Iterator> first,
        beast::detail::aged_container_iterator<is_const, Iterator> last)
{
    for (; first != last;)
        unlink_and_delete_element(&*((first++).iterator()));

    return beast::detail::aged_container_iterator<false, Iterator>(
        first.iterator());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class K>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    erase(K const& k) -> size_type
{
    auto iter(m_cont.find(k, std::cref(m_config.key_compare())));
    if (iter == m_cont.end())
        return 0;
    size_type n(0);
    for (;;)
    {
        auto p(&*iter++);
        bool const done(m_config(*p, extract(iter->value)));
        unlink_and_delete_element(p);
        ++n;
        if (done)
            break;
    }
    return n;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
void
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::swap(
    aged_ordered_container& other) noexcept
{
    swap_data(other);
    std::swap(chronological, other.chronological);
    std::swap(m_cont, other.m_cont);
}

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <class K>
auto
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    touch(K const& k) -> size_type
{
    auto const now(clock().now());
    size_type n(0);
    auto const range(equal_range(k));
    for (auto iter : range)
    {
        touch(iter, now);
        ++n;
    }
    return n;
}

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <
    bool OtherIsMulti,
    bool OtherIsMap,
    class OtherT,
    class OtherDuration,
    class OtherAllocator>
bool
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
operator==(aged_ordered_container<
           OtherIsMulti,
           OtherIsMap,
           Key,
           OtherT,
           OtherDuration,
           Compare,
           OtherAllocator> const& other) const
{
    using Other = aged_ordered_container<
        OtherIsMulti,
        OtherIsMap,
        Key,
        OtherT,
        OtherDuration,
        Compare,
        OtherAllocator>;
    if (size() != other.size())
        return false;
    std::equal_to<void> eq;
    return std::equal(
        cbegin(),
        cend(),
        other.cbegin(),
        other.cend(),
        [&eq, &other](
            value_type const& lhs, typename Other::value_type const& rhs) {
            return eq(extract(lhs), other.extract(rhs));
        });
}

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool is_const, class Iterator, class>
void
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    touch(
        beast::detail::aged_container_iterator<is_const, Iterator> pos,
        typename clock_type::time_point const& now)
{
    auto& e(*pos.iterator());
    e.when = now;
    chronological.list.erase(chronological.list.iterator_to(e));
    chronological.list.push_back(e);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_propagate>
typename std::enable_if<maybe_propagate>::type
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    swap_data(aged_ordered_container& other) noexcept
{
    std::swap(m_config.key_compare(), other.m_config.key_compare());
    std::swap(m_config.alloc(), other.m_config.alloc());
    std::swap(m_config.clock, other.m_config.clock);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
template <bool maybe_propagate>
typename std::enable_if<!maybe_propagate>::type
aged_ordered_container<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::
    swap_data(aged_ordered_container& other) noexcept
{
    std::swap(m_config.key_compare(), other.m_config.key_compare());
    std::swap(m_config.clock, other.m_config.clock);
}

}  // namespace detail

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
struct is_aged_container<beast::detail::aged_ordered_container<
    IsMulti,
    IsMap,
    Key,
    T,
    Clock,
    Compare,
    Allocator>> : std::true_type
{
    explicit is_aged_container() = default;
};

// Free functions

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator>
void
swap(
    beast::detail::aged_ordered_container<
        IsMulti,
        IsMap,
        Key,
        T,
        Clock,
        Compare,
        Allocator>& lhs,
    beast::detail::aged_ordered_container<
        IsMulti,
        IsMap,
        Key,
        T,
        Clock,
        Compare,
        Allocator>& rhs) noexcept
{
    lhs.swap(rhs);
}

/** Expire aged container items past the specified age. */
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Compare,
    class Allocator,
    class Rep,
    class Period>
std::size_t
expire(
    detail::aged_ordered_container<
        IsMulti,
        IsMap,
        Key,
        T,
        Clock,
        Compare,
        Allocator>& c,
    std::chrono::duration<Rep, Period> const& age)
{
    std::size_t n(0);
    auto const expired(c.clock().now() - age);
    for (auto iter(c.chronological.cbegin());
         iter != c.chronological.cend() && iter.when() <= expired;)
    {
        iter = c.erase(iter);
        ++n;
    }
    return n;
}

}  // namespace beast

#endif
