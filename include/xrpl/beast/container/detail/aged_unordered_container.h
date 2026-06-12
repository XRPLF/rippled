#pragma once

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/container/aged_container.h>
#include <xrpl/beast/container/detail/aged_associative_container.h>
#include <xrpl/beast/container/detail/aged_container_iterator.h>
#include <xrpl/beast/container/detail/empty_base_optimization.h>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <algorithm>
#include <cmath>
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
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class Allocator = std::allocator<std::conditional_t<IsMap, std::pair<Key const, T>, Key>>>
class AgedUnorderedContainer
{
public:
    using clock_type = AbstractClock<Clock>;
    using time_point = typename clock_type::time_point;
    using duration = typename clock_type::duration;
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::conditional_t<IsMap, std::pair<Key const, T>, Key>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    // Introspection (for unit tests)
    using is_unordered = std::true_type;
    using is_multi = std::integral_constant<bool, IsMulti>;
    using is_map = std::integral_constant<bool, IsMap>;

private:
    static Key const&
    extract(value_type const& value)
    {
        return AgedAssociativeContainerExtractT<IsMap>()(value);
    }

    // VFALCO TODO hoist to remove template argument dependencies
    struct Element : boost::intrusive::unordered_set_base_hook<
                         boost::intrusive::link_mode<boost::intrusive::normal_link>>,
                     boost::intrusive::list_base_hook<
                         boost::intrusive::link_mode<boost::intrusive::normal_link>>
    {
        // Stash types here so the iterator doesn't
        // need to see the container declaration.
        struct Stashed
        {
            explicit Stashed() = default;

            using value_type = typename AgedUnorderedContainer::value_type;
            using time_point = typename AgedUnorderedContainer::time_point;
        };

        Element(time_point const& when, value_type const& value) : value(value), when(when)
        {
        }

        Element(time_point const& when, value_type&& value) : value(std::move(value)), when(when)
        {
        }

        template <
            class... Args,
            class = std::enable_if_t<std::is_constructible_v<value_type, Args...>>>
        Element(time_point const& when, Args&&... args)
            : value(std::forward<Args>(args)...), when(when)
        {
        }

        value_type value;
        time_point when;
    };

    // VFALCO TODO hoist to remove template argument dependencies
    class ValueHash : public Hash
    {
    public:
        using argument_type = Element;
        using result_type = size_t;

        ValueHash() = default;

        ValueHash(Hash const& h) : Hash(h)
        {
        }

        std::size_t
        operator()(Element const& e) const
        {
            return Hash::operator()(extract(e.value));
        }

        Hash&
        hashFunction()
        {
            return *this;
        }

        [[nodiscard]] Hash const&
        hashFunction() const
        {
            return *this;
        }
    };

    // Compares value_type against element, used in find/insert_check
    // VFALCO TODO hoist to remove template argument dependencies
    class KeyValueEqual : public KeyEqual
    {
    public:
        using first_argument_type = Key;
        using second_argument_type = Element;
        using result_type = bool;

        KeyValueEqual() = default;

        KeyValueEqual(KeyEqual const& keyEqual) : KeyEqual(keyEqual)
        {
        }

        bool
        operator()(Key const& k, Element const& e) const
        {
            return KeyEqual::operator()(k, extract(e.value));
        }

        bool
        operator()(Element const& e, Key const& k) const
        {
            return KeyEqual::operator()(extract(e.value), k);
        }

        bool
        operator()(Element const& lhs, Element const& rhs) const
        {
            return KeyEqual::operator()(extract(lhs.value), extract(rhs.value));
        }

        KeyEqual&
        keyEq()
        {
            return *this;
        }

        [[nodiscard]] KeyEqual const&
        keyEq() const
        {
            return *this;
        }
    };

    using list_type = typename boost::intrusive::
        make_list<Element, boost::intrusive::constant_time_size<false>>::type;

    using cont_type = std::conditional_t<
        IsMulti,
        typename boost::intrusive::make_unordered_multiset<
            Element,
            boost::intrusive::constant_time_size<true>,
            boost::intrusive::hash<ValueHash>,
            boost::intrusive::equal<KeyValueEqual>,
            boost::intrusive::cache_begin<true>>::type,
        typename boost::intrusive::make_unordered_set<
            Element,
            boost::intrusive::constant_time_size<true>,
            boost::intrusive::hash<ValueHash>,
            boost::intrusive::equal<KeyValueEqual>,
            boost::intrusive::cache_begin<true>>::type>;

    using bucket_type = typename cont_type::bucket_type;
    using bucket_traits = typename cont_type::bucket_traits;

    using ElementAllocator =
        typename std::allocator_traits<Allocator>::template rebind_alloc<Element>;

    using ElementAllocatorTraits = std::allocator_traits<ElementAllocator>;

    using BucketAllocator =
        typename std::allocator_traits<Allocator>::template rebind_alloc<Element>;

    using BucketAllocatorTraits = std::allocator_traits<BucketAllocator>;

    class ConfigT : private ValueHash,
                    private KeyValueEqual,
                    private beast::detail::EmptyBaseOptimization<ElementAllocator>
    {
    public:
        explicit ConfigT(clock_type& clock) : clock(clock)
        {
        }

        ConfigT(clock_type& clock, Hash const& hash) : ValueHash(hash), clock(clock)
        {
        }

        ConfigT(clock_type& clock, KeyEqual const& keyEqual) : KeyValueEqual(keyEqual), clock(clock)
        {
        }

        ConfigT(clock_type& clock, Allocator const& alloc)
            : beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc), clock(clock)
        {
        }

        ConfigT(clock_type& clock, Hash const& hash, KeyEqual const& keyEqual)
            : ValueHash(hash), KeyValueEqual(keyEqual), clock(clock)
        {
        }

        ConfigT(clock_type& clock, Hash const& hash, Allocator const& alloc)
            : ValueHash(hash)
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(clock)
        {
        }

        ConfigT(clock_type& clock, KeyEqual const& keyEqual, Allocator const& alloc)
            : KeyValueEqual(keyEqual)
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(clock)
        {
        }

        ConfigT(
            clock_type& clock,
            Hash const& hash,
            KeyEqual const& keyEqual,
            Allocator const& alloc)
            : ValueHash(hash)
            , KeyValueEqual(keyEqual)
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(clock)
        {
        }

        ConfigT(ConfigT const& other)
            : ValueHash(other.hashFunction())
            , KeyValueEqual(other.keyEq())
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(
                  ElementAllocatorTraits::select_on_container_copy_construction(other.alloc()))
            , clock(other.clock)
        {
        }

        ConfigT(ConfigT const& other, Allocator const& alloc)
            : ValueHash(other.hashFunction())
            , KeyValueEqual(other.keyEq())
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(other.clock)
        {
        }

        ConfigT(ConfigT&& other)
            : ValueHash(std::move(other.hashFunction()))
            , KeyValueEqual(std::move(other.keyEq()))
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(std::move(other.alloc()))
            , clock(other.clock)
        {
        }

        ConfigT(
            ConfigT&& other,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
            Allocator const& alloc)
            : ValueHash(std::move(other.hashFunction()))
            , KeyValueEqual(std::move(other.keyEq()))
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(other.clock)
        {
        }

        ConfigT&
        operator=(ConfigT const& other)
        {
            hashFunction() = other.hashFunction();
            keyEq() = other.keyEq();
            alloc() = other.alloc();
            clock = other.clock;
            return *this;
        }

        ConfigT&
        operator=(ConfigT&& other)
        {
            hashFunction() = std::move(other.hashFunction());
            keyEq() = std::move(other.keyEq());
            alloc() = std::move(other.alloc());
            clock = other.clock;
            return *this;
        }

        ValueHash&
        valueHash()
        {
            return *this;
        }

        [[nodiscard]] ValueHash const&
        valueHash() const
        {
            return *this;
        }

        Hash&
        hashFunction()
        {
            return ValueHash::hashFunction();
        }

        [[nodiscard]] Hash const&
        hashFunction() const
        {
            return ValueHash::hashFunction();
        }

        KeyValueEqual&
        keyValueEqual()
        {
            return *this;
        }

        [[nodiscard]] KeyValueEqual const&
        keyValueEqual() const
        {
            return *this;
        }

        KeyEqual&
        keyEq()
        {
            return keyValueEqual().keyEq();
        }

        [[nodiscard]] KeyEqual const&
        keyEq() const
        {
            return keyValueEqual().keyEq();
        }

        ElementAllocator&
        alloc()
        {
            return beast::detail::EmptyBaseOptimization<ElementAllocator>::member();
        }

        [[nodiscard]] ElementAllocator const&
        alloc() const
        {
            return beast::detail::EmptyBaseOptimization<ElementAllocator>::member();
        }

        std::reference_wrapper<clock_type> clock;
    };

    class Buckets
    {
    public:
        using vec_type = std::vector<
            bucket_type,
            typename std::allocator_traits<Allocator>::template rebind_alloc<bucket_type>>;

        Buckets() : maxLoadFactor_(1.f), vec_()
        {
            vec_.resize(cont_type::suggested_upper_bucket_count(0));
        }

        Buckets(Allocator const& alloc) : maxLoadFactor_(1.f), vec_(alloc)
        {
            vec_.resize(cont_type::suggested_upper_bucket_count(0));
        }

        operator bucket_traits()
        {
            return bucket_traits(&vec_[0], vec_.size());
        }

        void
        clear()
        {
            vec_.clear();
        }

        [[nodiscard]] size_type
        maxBucketCount() const
        {
            return vec_.max_size();
        }

        float&
        maxLoadFactor()
        {
            return maxLoadFactor_;
        }

        [[nodiscard]] float const&
        maxLoadFactor() const
        {
            return maxLoadFactor_;
        }

        // count is the number of buckets
        template <class Container>
        void
        rehash(size_type count, Container& c)
        {
            size_type const size(vec_.size());
            if (count == size)
                return;
            if (count > vec_.capacity())
            {
                // Need two vectors otherwise we
                // will destroy non-empty buckets.
                vec_type vec(vec_.get_allocator());
                std::swap(vec_, vec);
                vec_.resize(count);
                c.rehash(bucket_traits(&vec_[0], vec_.size()));
                return;
            }
            // Rehash in place.
            if (count > size)
            {
                // This should not reallocate since
                // we checked capacity earlier.
                vec_.resize(count);
                c.rehash(bucket_traits(&vec_[0], count));
                return;
            }
            // Resize must happen after rehash otherwise
            // we might destroy non-empty buckets.
            c.rehash(bucket_traits(&vec_[0], count));
            vec_.resize(count);
        }

        // Resize the buckets to accommodate at least n items.
        template <class Container>
        void
        resize(size_type n, Container& c)
        {
            size_type const suggested(cont_type::suggested_upper_bucket_count(n));
            rehash(suggested, c);
        }

    private:
        float maxLoadFactor_;
        vec_type vec_;
    };

    template <class... Args>
    Element*
    newElement(Args&&... args)
    {
        struct Deleter
        {
            std::reference_wrapper<ElementAllocator> a;
            Deleter(ElementAllocator& a) : a(a)
            {
            }

            void
            operator()(Element* p)
            {
                ElementAllocatorTraits::deallocate(a.get(), p, 1);
            }
        };

        std::unique_ptr<Element, Deleter> p(
            ElementAllocatorTraits::allocate(config_.alloc(), 1), Deleter(config_.alloc()));
        ElementAllocatorTraits::construct(
            config_.alloc(), p.get(), clock().now(), std::forward<Args>(args)...);
        return p.release();
    }

    void
    deleteElement(Element const* p)
    {
        ElementAllocatorTraits::destroy(config_.alloc(), p);
        ElementAllocatorTraits::deallocate(config_.alloc(), const_cast<Element*>(p), 1);
    }

    void
    unlinkAndDeleteElement(Element const* p)
    {
        chronological.list_.erase(chronological.list_.iterator_to(*p));
        cont_.erase(cont_.iterator_to(*p));
        deleteElement(p);
    }

public:
    using hasher = Hash;
    using key_equal = KeyEqual;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;

    // A set iterator (IsMap==false) is always const
    // because the elements of a set are immutable.
    using iterator = beast::detail::AgedContainerIterator<!IsMap, typename cont_type::iterator>;
    using const_iterator = beast::detail::AgedContainerIterator<true, typename cont_type::iterator>;

    using local_iterator =
        beast::detail::AgedContainerIterator<!IsMap, typename cont_type::local_iterator>;
    using const_local_iterator =
        beast::detail::AgedContainerIterator<true, typename cont_type::local_iterator>;

    //--------------------------------------------------------------------------
    //
    // Chronological ordered iterators
    //
    // "Memberspace"
    // http://accu.org/index.php/journals/1527
    //
    //--------------------------------------------------------------------------

    class ChronologicalT
    {
    public:
        // A set iterator (IsMap==false) is always const
        // because the elements of a set are immutable.
        using iterator = beast::detail::AgedContainerIterator<!IsMap, typename list_type::iterator>;
        using const_iterator =
            beast::detail::AgedContainerIterator<true, typename list_type::iterator>;
        using reverse_iterator =
            beast::detail::AgedContainerIterator<!IsMap, typename list_type::reverse_iterator>;
        using const_reverse_iterator =
            beast::detail::AgedContainerIterator<true, typename list_type::reverse_iterator>;

        iterator
        begin()
        {
            return iterator(list_.begin());
        }

        const_iterator
        begin() const
        {
            return const_iterator(list_.begin());
        }

        const_iterator
        cbegin() const
        {
            return const_iterator(list_.begin());
        }

        iterator
        end()
        {
            return iterator(list_.end());
        }

        const_iterator
        end() const
        {
            return const_iterator(list_.end());
        }

        const_iterator
        cend() const
        {
            return const_iterator(list_.end());
        }

        reverse_iterator
        rbegin()
        {
            return reverse_iterator(list_.rbegin());
        }

        const_reverse_iterator
        rbegin() const
        {
            return const_reverse_iterator(list_.rbegin());
        }

        const_reverse_iterator
        crbegin() const
        {
            return const_reverse_iterator(list_.rbegin());
        }

        reverse_iterator
        rend()
        {
            return reverse_iterator(list_.rend());
        }

        const_reverse_iterator
        rend() const
        {
            return const_reverse_iterator(list_.rend());
        }

        const_reverse_iterator
        crend() const
        {
            return const_reverse_iterator(list_.rend());
        }

        iterator
        iteratorTo(value_type& value)
        {
            static_assert(std::is_standard_layout_v<Element>, "must be standard layout");
            return list_.iterator_to(*reinterpret_cast<Element*>(
                reinterpret_cast<uint8_t*>(&value) -
                ((std::size_t)std::addressof(((Element*)0)->member))));
        }

        const_iterator
        iteratorTo(value_type const& value) const
        {
            static_assert(std::is_standard_layout_v<Element>, "must be standard layout");
            return list_.iterator_to(*reinterpret_cast<Element const*>(
                reinterpret_cast<uint8_t const*>(&value) -
                ((std::size_t)std::addressof(((Element*)0)->member))));
        }

        ChronologicalT(ChronologicalT const&) = delete;
        ChronologicalT(ChronologicalT&&) = delete;
        ChronologicalT() = default;

    private:
        friend class AgedUnorderedContainer;
        list_type mutable list_;
    } chronological;

    //--------------------------------------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------------------------------------

    AgedUnorderedContainer() = delete;

    explicit AgedUnorderedContainer(clock_type& clock);

    AgedUnorderedContainer(clock_type& clock, Hash const& hash);

    AgedUnorderedContainer(clock_type& clock, KeyEqual const& keyEq);

    AgedUnorderedContainer(clock_type& clock, Allocator const& alloc);

    AgedUnorderedContainer(clock_type& clock, Hash const& hash, KeyEqual const& keyEq);

    AgedUnorderedContainer(clock_type& clock, Hash const& hash, Allocator const& alloc);

    AgedUnorderedContainer(clock_type& clock, KeyEqual const& keyEq, Allocator const& alloc);

    AgedUnorderedContainer(
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock, Hash const& hash);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock, KeyEqual const& keyEq);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock, Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Hash const& hash,
        Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    AgedUnorderedContainer(AgedUnorderedContainer const& other);

    AgedUnorderedContainer(AgedUnorderedContainer const& other, Allocator const& alloc);

    AgedUnorderedContainer(AgedUnorderedContainer&& other);

    AgedUnorderedContainer(
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
        AgedUnorderedContainer&& other,
        Allocator const& alloc);

    AgedUnorderedContainer(std::initializer_list<value_type> init, clock_type& clock);

    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash);

    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        KeyEqual const& keyEq);

    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Allocator const& alloc);

    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq);

    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash,
        Allocator const& alloc);

    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    ~AgedUnorderedContainer();

    AgedUnorderedContainer&
    operator=(AgedUnorderedContainer const& other);

    AgedUnorderedContainer&
    operator=(AgedUnorderedContainer&& other);

    AgedUnorderedContainer&
    operator=(std::initializer_list<value_type> init);

    allocator_type
    getAllocator() const
    {
        return config_.alloc();
    }

    clock_type&
    clock()
    {
        return config_.clock;
    }

    clock_type const&
    clock() const
    {
        return config_.clock;
    }

    //--------------------------------------------------------------------------
    //
    // Element access (maps)
    //
    //--------------------------------------------------------------------------

    template <
        class K,
        bool MaybeMulti = IsMulti,
        bool MaybeMap = IsMap,
        class = std::enable_if_t<MaybeMap && !MaybeMulti>>
    std::conditional_t<IsMap, T, void*>&
    at(K const& k);

    template <
        class K,
        bool MaybeMulti = IsMulti,
        bool MaybeMap = IsMap,
        class = std::enable_if_t<MaybeMap && !MaybeMulti>>
    typename std::conditional<IsMap, T, void*>::type const&
    at(K const& k) const;

    template <
        bool MaybeMulti = IsMulti,
        bool MaybeMap = IsMap,
        class = std::enable_if_t<MaybeMap && !MaybeMulti>>
    std::conditional_t<IsMap, T, void*>&
    operator[](Key const& key);

    template <
        bool MaybeMulti = IsMulti,
        bool MaybeMap = IsMap,
        class = std::enable_if_t<MaybeMap && !MaybeMulti>>
    std::conditional_t<IsMap, T, void*>&
    operator[](Key&& key);

    //--------------------------------------------------------------------------
    //
    // Iterators
    //
    //--------------------------------------------------------------------------

    iterator
    begin()
    {
        return iterator(cont_.begin());
    }

    const_iterator
    begin() const
    {
        return const_iterator(cont_.begin());
    }

    const_iterator
    cbegin() const
    {
        return const_iterator(cont_.begin());
    }

    iterator
    end()
    {
        return iterator(cont_.end());
    }

    const_iterator
    end() const
    {
        return const_iterator(cont_.end());
    }

    const_iterator
    cend() const
    {
        return const_iterator(cont_.end());
    }

    iterator
    iteratorTo(value_type& value)
    {
        static_assert(std::is_standard_layout_v<Element>, "must be standard layout");
        return cont_.iterator_to(*reinterpret_cast<Element*>(
            reinterpret_cast<uint8_t*>(&value) -
            ((std::size_t)std::addressof(((Element*)0)->member))));
    }

    const_iterator
    iteratorTo(value_type const& value) const
    {
        static_assert(std::is_standard_layout_v<Element>, "must be standard layout");
        return cont_.iterator_to(*reinterpret_cast<Element const*>(
            reinterpret_cast<uint8_t const*>(&value) -
            ((std::size_t)std::addressof(((Element*)0)->member))));
    }

    //--------------------------------------------------------------------------
    //
    // Capacity
    //
    //--------------------------------------------------------------------------

    bool
    empty() const noexcept
    {
        return cont_.empty();
    }

    size_type
    size() const noexcept
    {
        return cont_.size();
    }

    size_type
    maxSize() const noexcept
    {
        return config_.max_size();
    }

    //--------------------------------------------------------------------------
    //
    // Modifiers
    //
    //--------------------------------------------------------------------------

    void
    clear();

    // map, set
    template <bool MaybeMulti = IsMulti>
    auto
    insert(value_type const& value) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>;

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    auto
    insert(value_type const& value) -> std::enable_if_t<MaybeMulti, iterator>;

    // map, set
    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    auto
    insert(value_type&& value)
        -> std::enable_if_t<!MaybeMulti && !MaybeMap, std::pair<iterator, bool>>;

    // multimap, multiset
    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    auto
    insert(value_type&& value) -> std::enable_if_t<MaybeMulti && !MaybeMap, iterator>;

    // map, set
    template <bool MaybeMulti = IsMulti>
    std::enable_if_t<!MaybeMulti, iterator>
    insert(const_iterator /*hint*/, value_type const& value)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert(value).first;
    }

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    std::enable_if_t<MaybeMulti, iterator>
    insert(const_iterator /*hint*/, value_type const& value)
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert(value);
    }

    // map, set
    template <bool MaybeMulti = IsMulti>
    std::enable_if_t<!MaybeMulti, iterator>
    insert(const_iterator /*hint*/, value_type&& value)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert(std::move(value)).first;
    }

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    std::enable_if_t<MaybeMulti, iterator>
    insert(const_iterator /*hint*/, value_type&& value)
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert(std::move(value));
    }

    // map, multimap
    template <class P, bool MaybeMap = IsMap>
    std::enable_if_t<
        MaybeMap && std::is_constructible_v<value_type, P&&>,
        std::conditional_t<IsMulti, iterator, std::pair<iterator, bool>>>
    insert(P&& value)
    {
        return emplace(std::forward<P>(value));
    }

    // map, multimap
    template <class P, bool MaybeMap = IsMap>
    std::enable_if_t<
        MaybeMap && std::is_constructible_v<value_type, P&&>,
        std::conditional_t<IsMulti, iterator, std::pair<iterator, bool>>>
    insert(const_iterator hint, P&& value)
    {
        return emplaceHint(hint, std::forward<P>(value));
    }

    template <class InputIt>
    void
    insert(InputIt first, InputIt last)
    {
        insert(first, last, typename std::iterator_traits<InputIt>::iterator_category());
    }

    void
    insert(std::initializer_list<value_type> init)
    {
        insert(init.begin(), init.end());
    }

    // set, map
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplace(Args&&... args) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>;

    // multiset, multimap
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplace(Args&&... args) -> std::enable_if_t<MaybeMulti, iterator>;

    // set, map
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplaceHint(const_iterator /*hint*/, Args&&... args)
        -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>;

    // multiset, multimap
    template <bool MaybeMulti = IsMulti, class... Args>
    std::enable_if_t<MaybeMulti, iterator>
    emplaceHint(const_iterator /*hint*/, Args&&... args)
    {
        // VFALCO TODO The hint could be used for multi, to let
        //             the client order equal ranges
        return emplace<MaybeMulti>(std::forward<Args>(args)...);
    }

    template <bool IsConst, class Iterator>
    beast::detail::AgedContainerIterator<false, Iterator>
    erase(beast::detail::AgedContainerIterator<IsConst, Iterator> pos);

    template <bool IsConst, class Iterator>
    beast::detail::AgedContainerIterator<false, Iterator>
    erase(
        beast::detail::AgedContainerIterator<IsConst, Iterator> first,
        beast::detail::AgedContainerIterator<IsConst, Iterator> last);

    template <class K>
    auto
    erase(K const& k) -> size_type;

    void
    swap(AgedUnorderedContainer& other) noexcept;

    template <bool IsConst, class Iterator>
    void
    touch(beast::detail::AgedContainerIterator<IsConst, Iterator> pos)
    {
        touch(pos, clock().now());
    }

    template <class K>
    auto
    touch(K const& k) -> size_type;

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
        return cont_.count(
            k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    find(K const& k)
    {
        return iterator(
            cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    find(K const& k) const
    {
        return const_iterator(
            cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<iterator, iterator>
    equalRange(K const& k)
    {
        auto const r(cont_.equal_range(
            k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
        return std::make_pair(iterator(r.first), iterator(r.second));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<const_iterator, const_iterator>
    equalRange(K const& k) const
    {
        auto const r(cont_.equal_range(
            k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
        return std::make_pair(const_iterator(r.first), const_iterator(r.second));
    }

    //--------------------------------------------------------------------------
    //
    // Bucket interface
    //
    //--------------------------------------------------------------------------

    local_iterator
    begin(size_type n)
    {
        return local_iterator(cont_.begin(n));
    }

    const_local_iterator
    begin(size_type n) const
    {
        return const_local_iterator(cont_.begin(n));
    }

    const_local_iterator
    cbegin(size_type n) const
    {
        return const_local_iterator(cont_.begin(n));
    }

    local_iterator
    end(size_type n)
    {
        return local_iterator(cont_.end(n));
    }

    const_local_iterator
    end(size_type n) const
    {
        return const_local_iterator(cont_.end(n));
    }

    const_local_iterator
    cend(size_type n) const
    {
        return const_local_iterator(cont_.end(n));
    }

    size_type
    bucketCount() const
    {
        return cont_.bucket_count();
    }

    size_type
    maxBucketCount() const
    {
        return buck_.maxBucketCount();
    }

    size_type
    bucketSize(size_type n) const
    {
        return cont_.bucket_size(n);
    }

    size_type
    bucket(Key const& k) const
    {
        XRPL_ASSERT(
            bucketCount() != 0,
            "beast::detail::AgedUnorderedContainer::bucket : nonzero bucket "
            "count");
        return cont_.bucket(k, std::cref(config_.hashFunction()));
    }

    //--------------------------------------------------------------------------
    //
    // Hash policy
    //
    //--------------------------------------------------------------------------

    float
    loadFactor() const
    {
        return size() / static_cast<float>(cont_.bucket_count());
    }

    float
    maxLoadFactor() const
    {
        return buck_.maxLoadFactor();
    }

    void
    maxLoadFactor(float ml)
    {
        buck_.maxLoadFactor() = std::max(ml, buck_.maxLoadFactor());
    }

    void
    rehash(size_type count)
    {
        count = std::max(count, size_type(size() / maxLoadFactor()));
        buck_.rehash(count, cont_);
    }

    void
    reserve(size_type count)
    {
        rehash(std::ceil(count / maxLoadFactor()));
    }

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    hasher const&
    hashFunction() const
    {
        return config_.hashFunction();
    }

    key_equal const&
    keyEq() const
    {
        return config_.keyEq();
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
        bool MaybeMulti = IsMulti>
    std::enable_if_t<!MaybeMulti, bool>
    operator==(AgedUnorderedContainer<
               false,
               OtherIsMap,
               OtherKey,
               OtherT,
               OtherDuration,
               OtherHash,
               KeyEqual,
               OtherAllocator> const& other) const;

    template <
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherAllocator,
        bool MaybeMulti = IsMulti>
    std::enable_if_t<MaybeMulti, bool>
    operator==(AgedUnorderedContainer<
               true,
               OtherIsMap,
               OtherKey,
               OtherT,
               OtherDuration,
               OtherHash,
               KeyEqual,
               OtherAllocator> const& other) const;

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherAllocator>
    bool
    operator!=(AgedUnorderedContainer<
               OtherIsMulti,
               OtherIsMap,
               OtherKey,
               OtherT,
               OtherDuration,
               OtherHash,
               KeyEqual,
               OtherAllocator> const& other) const
    {
        return !(this->operator==(other));
    }

private:
    bool
    wouldExceed(size_type additional) const
    {
        return size() + additional > bucketCount() * maxLoadFactor();
    }

    void
    maybeRehash(size_type additional)
    {
        if (wouldExceed(additional))
            buck_.resize(size() + additional, cont_);
        XRPL_ASSERT(
            loadFactor() <= maxLoadFactor(),
            "beast::detail::AgedUnorderedContainer::maybeRehash : maximum "
            "load factor");
    }

    // map, set
    template <bool MaybeMulti = IsMulti>
    auto
    insertUnchecked(value_type const& value)
        -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>;

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    auto
    insertUnchecked(value_type const& value) -> std::enable_if_t<MaybeMulti, iterator>;

    template <class InputIt>
    void
    insertUnchecked(InputIt first, InputIt last)
    {
        for (; first != last; ++first)
            insertUnchecked(*first);
    }

    template <class InputIt>
    void
    insert(InputIt first, InputIt last, std::input_iterator_tag)
    {
        for (; first != last; ++first)
            insert(*first);
    }

    template <class InputIt>
    void
    insert(InputIt first, InputIt last, std::random_access_iterator_tag)
    {
        auto const n(std::distance(first, last));
        maybeRehash(n);
        insertUnchecked(first, last);
    }

    template <bool IsConst, class Iterator>
    void
    touch(
        beast::detail::AgedContainerIterator<IsConst, Iterator> pos,
        typename clock_type::time_point const& now)
    {
        auto& e(*pos.iterator());
        e.when = now;
        chronological.list_.erase(chronological.list_.iterator_to(e));
        chronological.list_.push_back(e);
    }

    template <
        bool MaybePropagate = std::allocator_traits<Allocator>::propagate_on_container_swap::value>
    std::enable_if_t<MaybePropagate>
    swapData(AgedUnorderedContainer& other) noexcept
    {
        std::swap(config_.hashFunction(), other.config_.hashFunction());
        std::swap(config_.keyEq(), other.config_.keyEq());
        std::swap(config_.alloc(), other.config_.alloc());
        std::swap(config_.clock, other.config_.clock);
    }

    template <
        bool MaybePropagate = std::allocator_traits<Allocator>::propagate_on_container_swap::value>
    std::enable_if_t<!MaybePropagate>
    swapData(AgedUnorderedContainer& other) noexcept
    {
        std::swap(config_.hashFunction(), other.config_.hashFunction());
        std::swap(config_.keyEq(), other.config_.keyEq());
        std::swap(config_.clock, other.config_.clock);
    }

private:
    ConfigT config_;
    Buckets buck_;
    cont_type mutable cont_;
};

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(clock_type& clock)
    : config_(clock)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(clock_type& clock, Hash const& hash)
    : config_(clock, hash)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(clock_type& clock, KeyEqual const& keyEq)
    : config_(clock, keyEq)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(clock_type& clock, Allocator const& alloc)
    : config_(clock, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(clock_type& clock, Hash const& hash, KeyEqual const& keyEq)
    : config_(clock, hash, keyEq)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(clock_type& clock, Hash const& hash, Allocator const& alloc)
    : config_(clock, hash, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(clock_type& clock, KeyEqual const& keyEq, Allocator const& alloc)
    : config_(clock, keyEq, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc)
    : config_(clock, hash, keyEq, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock)
    : config_(clock)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock, Hash const& hash)
    : config_(clock, hash)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock, KeyEqual const& keyEq)
    : config_(clock, keyEq)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(InputIt first, InputIt last, clock_type& clock, Allocator const& alloc)
    : config_(clock, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq)
    : config_(clock, hash, keyEq)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Hash const& hash,
        Allocator const& alloc)
    : config_(clock, hash, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        KeyEqual const& keyEq,
        Allocator const& alloc)
    : config_(clock, keyEq, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class InputIt>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc)
    : config_(clock, hash, keyEq, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(first, last);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(AgedUnorderedContainer const& other)
    : config_(other.config_)
    , buck_(config_.alloc())
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(other.cbegin(), other.cend());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(AgedUnorderedContainer const& other, Allocator const& alloc)
    : config_(other.config_, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(other.cbegin(), other.cend());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(AgedUnorderedContainer&& other)
    : config_(std::move(other.config_))
    , buck_(std::move(other.buck_))
    , cont_(std::move(other.cont_))
{
    chronological.list_ = std::move(other.chronological.list_);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
        AgedUnorderedContainer&& other,
        Allocator const& alloc)
    : config_(std::move(other.config_), alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
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
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(std::initializer_list<value_type> init, clock_type& clock)
    : config_(clock)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash)
    : config_(clock, hash)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        KeyEqual const& keyEq)
    : config_(clock, keyEq)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Allocator const& alloc)
    : config_(clock, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq)
    : config_(clock, hash, keyEq)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash,
        Allocator const& alloc)
    : config_(clock, hash, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        KeyEqual const& keyEq,
        Allocator const& alloc)
    : config_(clock, keyEq, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    AgedUnorderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc)
    : config_(clock, hash, keyEq, alloc)
    , buck_(alloc)
    , cont_(buck_, std::cref(config_.valueHash()), std::cref(config_.keyValueEqual()))
{
    insert(init.begin(), init.end());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::
    ~AgedUnorderedContainer()
{
    clear();
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator=(
    AgedUnorderedContainer const& other) -> AgedUnorderedContainer&
{
    if (this != &other)
    {
        size_type const n(other.size());
        clear();
        config_ = other.config_;
        buck_ = Buckets(config_.alloc());
        maybeRehash(n);
        insertUnchecked(other.begin(), other.end());
    }
    return *this;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator=(
    AgedUnorderedContainer&& other) -> AgedUnorderedContainer&
{
    size_type const n(other.size());
    clear();
    config_ = std::move(other.config_);
    buck_ = Buckets(config_.alloc());
    maybeRehash(n);
    insertUnchecked(other.begin(), other.end());
    other.clear();
    return *this;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator=(
    std::initializer_list<value_type> init) -> AgedUnorderedContainer&
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
    class Hash,
    class KeyEqual,
    class Allocator>
template <class K, bool MaybeMulti, bool MaybeMap, class>
std::conditional_t<IsMap, T, void*>&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::at(K const& k)
{
    auto const iter(
        cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    if (iter == cont_.end())
        throw std::out_of_range("key not found");
    return iter->value.second;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class K, bool MaybeMulti, bool MaybeMap, class>
typename std::conditional<IsMap, T, void*>::type const&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::at(
    K const& k) const
{
    auto const iter(
        cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    if (iter == cont_.end())
        throw std::out_of_range("key not found");
    return iter->value.second;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti, bool MaybeMap, class>
std::conditional_t<IsMap, T, void*>&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator[](
    Key const& key)
{
    maybeRehash(1);
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(
        key, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(
            std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple()));
        cont_.insert_commit(*p, d);
        chronological.list_.push_back(*p);
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
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti, bool MaybeMap, class>
std::conditional_t<IsMap, T, void*>&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator[](
    Key&& key)
{
    maybeRehash(1);
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(
        key, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(
            std::piecewise_construct,
            std::forward_as_tuple(std::move(key)),
            std::forward_as_tuple()));
        cont_.insert_commit(*p, d);
        chronological.list_.push_back(*p);
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
    class Hash,
    class KeyEqual,
    class Allocator>
void
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::clear()
{
    for (auto iter(chronological.list_.begin()); iter != chronological.list_.end();)
        unlinkAndDeleteElement(&*iter++);
    chronological.list_.clear();
    cont_.clear();
    buck_.clear();
}

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::insert(
    value_type const& value) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>
{
    maybeRehash(1);
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(value), std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(value));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
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
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::insert(
    value_type const& value) -> std::enable_if_t<MaybeMulti, iterator>
{
    maybeRehash(1);
    Element* const p(newElement(value));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return iterator(iter);
}

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti, bool MaybeMap>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::insert(
    value_type&& value) -> std::enable_if_t<!MaybeMulti && !MaybeMap, std::pair<iterator, bool>>
{
    maybeRehash(1);
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(value), std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(std::move(value)));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
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
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti, bool MaybeMap>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::insert(
    value_type&& value) -> std::enable_if_t<MaybeMulti && !MaybeMap, iterator>
{
    maybeRehash(1);
    Element* const p(newElement(std::move(value)));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return iterator(iter);
}

#if 1  // Use insert() instead of insert_check() insert_commit()
// set, map
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti, class... Args>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::emplace(
    Args&&... args) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>
{
    maybeRehash(1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    Element* const p(newElement(std::forward<Args>(args)...));
    auto const result(cont_.insert(*p));
    if (result.second)
    {
        chronological.list_.push_back(*p);
        return std::make_pair(iterator(result.first), true);
    }
    deleteElement(p);
    return std::make_pair(iterator(result.first), false);
}
#else   // As original, use insert_check() / insert_commit () pair.
// set, map
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool maybe_multi, class... Args>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::emplace(
    Args&&... args) -> typename std::enable_if<!maybe_multi, std::pair<iterator, bool>>::type
{
    maybe_rehash(1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    element* const p(new_element(std::forward<Args>(args)...));
    typename cont_type::insert_commit_data d;
    auto const result(m_cont.insert_check(
        extract(p->value),
        std::cref(m_config.hashFunction()),
        std::cref(m_config.keyValueEqual()),
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
#endif  // 0

// multiset, multimap
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti, class... Args>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::emplace(
    Args&&... args) -> std::enable_if_t<MaybeMulti, iterator>
{
    maybeRehash(1);
    Element* const p(newElement(std::forward<Args>(args)...));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return iterator(iter);
}

// set, map
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti, class... Args>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::emplaceHint(
    const_iterator /*hint*/,
    Args&&... args) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>
{
    maybeRehash(1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    Element* const p(newElement(std::forward<Args>(args)...));
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(p->value),
        std::cref(config_.hashFunction()),
        std::cref(config_.keyValueEqual()),
        d));
    if (result.second)
    {
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    deleteElement(p);
    return std::make_pair(iterator(result.first), false);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool IsConst, class Iterator>
beast::detail::AgedContainerIterator<false, Iterator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::erase(
    beast::detail::AgedContainerIterator<IsConst, Iterator> pos)
{
    unlinkAndDeleteElement(&*((pos++).iterator()));
    return beast::detail::AgedContainerIterator<false, Iterator>(pos.iterator());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool IsConst, class Iterator>
beast::detail::AgedContainerIterator<false, Iterator>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::erase(
    beast::detail::AgedContainerIterator<IsConst, Iterator> first,
    beast::detail::AgedContainerIterator<IsConst, Iterator> last)
{
    for (; first != last;)
        unlinkAndDeleteElement(&*((first++).iterator()));

    return beast::detail::AgedContainerIterator<false, Iterator>(first.iterator());
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class K>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::erase(K const& k)
    -> size_type
{
    auto iter(cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    if (iter == cont_.end())
        return 0;
    size_type n(0);
    for (;;)
    {
        auto p(&*iter++);
        bool const done(config_(*p, extract(iter->value)));
        unlinkAndDeleteElement(p);
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
    class Hash,
    class KeyEqual,
    class Allocator>
void
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::swap(
    AgedUnorderedContainer& other) noexcept
{
    swapData(other);
    std::swap(chronological, other.chronological);
    std::swap(cont_, other.cont_);
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <class K>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::touch(K const& k)
    -> size_type
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

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <
    bool OtherIsMap,
    class OtherKey,
    class OtherT,
    class OtherDuration,
    class OtherHash,
    class OtherAllocator,
    bool MaybeMulti>
std::enable_if_t<!MaybeMulti, bool>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator==(
    AgedUnorderedContainer<
        false,
        OtherIsMap,
        OtherKey,
        OtherT,
        OtherDuration,
        OtherHash,
        KeyEqual,
        OtherAllocator> const& other) const
{
    if (size() != other.size())
        return false;
    for (auto iter(cbegin()), last(cend()), otherLast(other.cend()); iter != last; ++iter)
    {
        auto otherIter(other.find(extract(*iter)));
        if (otherIter == otherLast)
            return false;
    }
    return true;
}

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <
    bool OtherIsMap,
    class OtherKey,
    class OtherT,
    class OtherDuration,
    class OtherHash,
    class OtherAllocator,
    bool MaybeMulti>
std::enable_if_t<MaybeMulti, bool>
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator==(
    AgedUnorderedContainer<
        true,
        OtherIsMap,
        OtherKey,
        OtherT,
        OtherDuration,
        OtherHash,
        KeyEqual,
        OtherAllocator> const& other) const
{
    if (size() != other.size())
        return false;
    for (auto iter(cbegin()), last(cend()); iter != last;)
    {
        auto const& k(extract(*iter));
        auto const eq(equalRange(k));
        auto const oeq(other.equalRange(k));
#if BEAST_NO_CXX14_IS_PERMUTATION
        if (std::distance(eq.first, eq.second) != std::distance(oeq.first, oeq.second) ||
            !std::is_permutation(eq.first, eq.second, oeq.first))
            return false;
#else
        if (!std::is_permutation(eq.first, eq.second, oeq.first, oeq.second))
            return false;
#endif
        iter = eq.second;
    }
    return true;
}

//------------------------------------------------------------------------------

// map, set
template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::insertUnchecked(
    value_type const& value) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>
{
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(value), std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(value));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
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
    class Hash,
    class KeyEqual,
    class Allocator>
template <bool MaybeMulti>
auto
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::insertUnchecked(
    value_type const& value) -> std::enable_if_t<MaybeMulti, iterator>
{
    Element* const p(newElement(value));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return iterator(iter);
}

//------------------------------------------------------------------------------

}  // namespace detail

//------------------------------------------------------------------------------

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
struct IsAgedContainer<
    beast::detail::AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>>
    : std::true_type
{
    explicit IsAgedContainer() = default;
};

// Free functions

template <
    bool IsMulti,
    bool IsMap,
    class Key,
    class T,
    class Clock,
    class Hash,
    class KeyEqual,
    class Allocator>
void
swap(
    beast::detail::AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>&
        lhs,
    beast::detail::AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>&
        rhs) noexcept
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
    class Hash,
    class KeyEqual,
    class Allocator,
    class Rep,
    class Period>
std::size_t
expire(
    beast::detail::AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>&
        c,
    std::chrono::duration<Rep, Period> const& age) noexcept
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
