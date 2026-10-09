#pragma once

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/container/aged_container.h>
#include <xrpl/beast/container/detail/aged_associative_container.h>
#include <xrpl/beast/container/detail/aged_container_iterator.h>
#include <xrpl/beast/container/detail/empty_base_optimization.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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

/**
 * Associative container where each element is also indexed by time.
 *
 * This container mirrors the interface of the standard library unordered
 * associative containers, with the addition that each element is associated
 * with a `when` `time_point` which is obtained from the value of the clock's
 * `now`. The function `touch` updates the time for an element to the current
 * time as reported by the clock.
 *
 * An extra set of iterator types and member functions are provided in the
 * `chronological` memberspace that allow traversal in temporal or reverse
 * temporal order. This container is useful as a building block for caches
 * whose items expire after a certain amount of time. The chronological
 * iterators allow for fully customizable expiration strategies.
 *
 * @see AgedUnorderedSet, AgedUnorderedMultiset
 * @see AgedUnorderedMap, AgedUnorderedMultimap
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
    using ClockType = AbstractClock<Clock>;
    using TimePoint = ClockType::time_point;
    using Duration = ClockType::duration;
    using KeyType = Key;
    using MappedType = T;
    using ValueType = std::conditional_t<IsMap, std::pair<Key const, T>, Key>;
    using SizeType = std::size_t;
    using DifferenceType = std::ptrdiff_t;

    // Introspection (for unit tests)
    using IsUnorderedType = std::true_type;
    using IsMultiType = std::integral_constant<bool, IsMulti>;
    using IsMapType = std::integral_constant<bool, IsMap>;

private:
    static Key const&
    extract(ValueType const& value)
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

            using ValueType = AgedUnorderedContainer::ValueType;
            using TimePoint = AgedUnorderedContainer::TimePoint;
        };

        Element(TimePoint const& when, ValueType const& value) : value(value), when(when)
        {
        }

        Element(TimePoint const& when, ValueType&& value) : value(std::move(value)), when(when)
        {
        }

        template <class... Args>
        Element(TimePoint const& when, Args&&... args)
            requires(std::is_constructible_v<ValueType, Args...>)
            : value(std::forward<Args>(args)...), when(when)
        {
        }

        ValueType value;
        TimePoint when;
    };

    // VFALCO TODO hoist to remove template argument dependencies
    class ValueHash : public Hash
    {
    public:
        using ArgumentType = Element;
        using ResultType = size_t;

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
        using FirstArgumentType = Key;
        using SecondArgumentType = Element;
        using ResultType = bool;

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

    using ListType =
        boost::intrusive::make_list<Element, boost::intrusive::constant_time_size<false>>::type;

    using ContType = std::conditional_t<
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

    using BucketType = ContType::bucket_type;
    using BucketTraits = ContType::bucket_traits;

    using ElementAllocator = std::allocator_traits<Allocator>::template rebind_alloc<Element>;

    using ElementAllocatorTraits = std::allocator_traits<ElementAllocator>;

    using BucketAllocator = std::allocator_traits<Allocator>::template rebind_alloc<Element>;

    using BucketAllocatorTraits = std::allocator_traits<BucketAllocator>;

    class ConfigT : private ValueHash,
                    private KeyValueEqual,
                    private beast::detail::EmptyBaseOptimization<ElementAllocator>
    {
    public:
        explicit ConfigT(ClockType& clock) : clock(clock)
        {
        }

        ConfigT(ClockType& clock, Hash const& hash) : ValueHash(hash), clock(clock)
        {
        }

        ConfigT(ClockType& clock, KeyEqual const& keyEqual) : KeyValueEqual(keyEqual), clock(clock)
        {
        }

        ConfigT(ClockType& clock, Allocator const& alloc)
            : beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc), clock(clock)
        {
        }

        ConfigT(ClockType& clock, Hash const& hash, KeyEqual const& keyEqual)
            : ValueHash(hash), KeyValueEqual(keyEqual), clock(clock)
        {
        }

        ConfigT(ClockType& clock, Hash const& hash, Allocator const& alloc)
            : ValueHash(hash)
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(clock)
        {
        }

        ConfigT(ClockType& clock, KeyEqual const& keyEqual, Allocator const& alloc)
            : KeyValueEqual(keyEqual)
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(clock)
        {
        }

        ConfigT(
            ClockType& clock,
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

        std::reference_wrapper<ClockType> clock;
    };

    class Buckets
    {
    public:
        using VecType = std::vector<
            BucketType,
            typename std::allocator_traits<Allocator>::template rebind_alloc<BucketType>>;

        Buckets() : maxLoadFactor_(1.f), vec_()
        {
            vec_.resize(ContType::suggested_upper_bucket_count(0));
        }

        Buckets(Allocator const& alloc) : maxLoadFactor_(1.f), vec_(alloc)
        {
            vec_.resize(ContType::suggested_upper_bucket_count(0));
        }

        operator BucketTraits()
        {
            return BucketTraits(&vec_[0], vec_.size());
        }

        void
        clear()
        {
            vec_.clear();
        }

        [[nodiscard]] SizeType
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
        rehash(SizeType count, Container& c)
        {
            SizeType const size(vec_.size());
            if (count == size)
                return;
            if (count > vec_.capacity())
            {
                // Need two vectors otherwise we
                // will destroy non-empty buckets.
                VecType vec(vec_.get_allocator());
                std::swap(vec_, vec);
                vec_.resize(count);
                c.rehash(BucketTraits(&vec_[0], vec_.size()));
                return;
            }
            // Rehash in place.
            if (count > size)
            {
                // This should not reallocate since
                // we checked capacity earlier.
                vec_.resize(count);
                c.rehash(BucketTraits(&vec_[0], count));
                return;
            }
            // Resize must happen after rehash otherwise
            // we might destroy non-empty buckets.
            c.rehash(BucketTraits(&vec_[0], count));
            vec_.resize(count);
        }

        // Resize the buckets to accommodate at least n items.
        template <class Container>
        void
        resize(SizeType n, Container& c)
        {
            SizeType const suggested(ContType::suggested_upper_bucket_count(n));
            rehash(suggested, c);
        }

    private:
        float maxLoadFactor_;
        VecType vec_;
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
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
    using Hasher = Hash;
    // Named KeyEq, not KeyEqual: the latter is this template's own parameter,
    // and redeclaring a template parameter in its scope is ill-formed.
    using KeyEq = KeyEqual;
    using AllocatorType = Allocator;
    using Reference = ValueType&;
    using ConstReference = ValueType const&;
    using Pointer = std::allocator_traits<Allocator>::pointer;
    using ConstPointer = std::allocator_traits<Allocator>::const_pointer;

    // A set iterator (IsMap==false) is always const
    // because the elements of a set are immutable.
    using Iterator = beast::detail::AgedContainerIterator<!IsMap, typename ContType::iterator>;
    using ConstIterator = beast::detail::AgedContainerIterator<true, typename ContType::iterator>;

    using LocalIterator =
        beast::detail::AgedContainerIterator<!IsMap, typename ContType::local_iterator>;
    using ConstLocalIterator =
        beast::detail::AgedContainerIterator<true, typename ContType::local_iterator>;

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
        using Iterator = beast::detail::AgedContainerIterator<!IsMap, typename ListType::iterator>;
        using ConstIterator =
            beast::detail::AgedContainerIterator<true, typename ListType::iterator>;
        using ReverseIterator =
            beast::detail::AgedContainerIterator<!IsMap, typename ListType::reverse_iterator>;
        using ConstReverseIterator =
            beast::detail::AgedContainerIterator<true, typename ListType::reverse_iterator>;

        Iterator
        begin()
        {
            return Iterator(list_.begin());
        }

        ConstIterator
        begin() const
        {
            return ConstIterator(list_.begin());
        }

        ConstIterator
        cbegin() const
        {
            return ConstIterator(list_.begin());
        }

        Iterator
        end()
        {
            return Iterator(list_.end());
        }

        ConstIterator
        end() const
        {
            return ConstIterator(list_.end());
        }

        ConstIterator
        cend() const
        {
            return ConstIterator(list_.end());
        }

        ReverseIterator
        rbegin()
        {
            return ReverseIterator(list_.rbegin());
        }

        ConstReverseIterator
        rbegin() const
        {
            return ConstReverseIterator(list_.rbegin());
        }

        ConstReverseIterator
        crbegin() const
        {
            return ConstReverseIterator(list_.rbegin());
        }

        ReverseIterator
        rend()
        {
            return ReverseIterator(list_.rend());
        }

        ConstReverseIterator
        rend() const
        {
            return ConstReverseIterator(list_.rend());
        }

        ConstReverseIterator
        crend() const
        {
            return ConstReverseIterator(list_.rend());
        }

        Iterator
        iteratorTo(ValueType& value)
        {
            static_assert(std::is_standard_layout_v<Element>, "must be standard layout");
            return list_.iterator_to(*reinterpret_cast<Element*>(
                reinterpret_cast<uint8_t*>(&value) -
                ((std::size_t)std::addressof(((Element*)0)->member))));
        }

        ConstIterator
        iteratorTo(ValueType const& value) const
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
        ListType mutable list_;
    } chronological;

    //--------------------------------------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------------------------------------

    AgedUnorderedContainer() = delete;

    explicit AgedUnorderedContainer(ClockType& clock);

    AgedUnorderedContainer(ClockType& clock, Hash const& hash);

    AgedUnorderedContainer(ClockType& clock, KeyEqual const& keyEq);

    AgedUnorderedContainer(ClockType& clock, Allocator const& alloc);

    AgedUnorderedContainer(ClockType& clock, Hash const& hash, KeyEqual const& keyEq);

    AgedUnorderedContainer(ClockType& clock, Hash const& hash, Allocator const& alloc);

    AgedUnorderedContainer(ClockType& clock, KeyEqual const& keyEq, Allocator const& alloc);

    AgedUnorderedContainer(
        ClockType& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock, Hash const& hash);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock, KeyEqual const& keyEq);

    template <class InputIt>
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock, Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        ClockType& clock,
        Hash const& hash,
        KeyEqual const& keyEq);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        ClockType& clock,
        Hash const& hash,
        Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        ClockType& clock,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    template <class InputIt>
    AgedUnorderedContainer(
        InputIt first,
        InputIt last,
        ClockType& clock,
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

    AgedUnorderedContainer(std::initializer_list<ValueType> init, ClockType& clock);

    AgedUnorderedContainer(
        std::initializer_list<ValueType> init,
        ClockType& clock,
        Hash const& hash);

    AgedUnorderedContainer(
        std::initializer_list<ValueType> init,
        ClockType& clock,
        KeyEqual const& keyEq);

    AgedUnorderedContainer(
        std::initializer_list<ValueType> init,
        ClockType& clock,
        Allocator const& alloc);

    AgedUnorderedContainer(
        std::initializer_list<ValueType> init,
        ClockType& clock,
        Hash const& hash,
        KeyEqual const& keyEq);

    AgedUnorderedContainer(
        std::initializer_list<ValueType> init,
        ClockType& clock,
        Hash const& hash,
        Allocator const& alloc);

    AgedUnorderedContainer(
        std::initializer_list<ValueType> init,
        ClockType& clock,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    AgedUnorderedContainer(
        std::initializer_list<ValueType> init,
        ClockType& clock,
        Hash const& hash,
        KeyEqual const& keyEq,
        Allocator const& alloc);

    ~AgedUnorderedContainer();

    AgedUnorderedContainer&
    operator=(AgedUnorderedContainer const& other);

    AgedUnorderedContainer&
    operator=(AgedUnorderedContainer&& other);

    AgedUnorderedContainer&
    operator=(std::initializer_list<ValueType> init);

    AllocatorType
    getAllocator() const
    {
        return config_.alloc();
    }

    ClockType&
    clock()
    {
        return config_.clock;
    }

    ClockType const&
    clock() const
    {
        return config_.clock;
    }

    //--------------------------------------------------------------------------
    //
    // Element access (maps)
    //
    //--------------------------------------------------------------------------

    template <class K, bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    std::conditional_t<IsMap, T, void*>&
    at(K const& k)
        requires(MaybeMap && !MaybeMulti);

    template <class K, bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    std::conditional<IsMap, T, void*>::type const&
    at(K const& k) const
        requires(MaybeMap && !MaybeMulti);

    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    std::conditional_t<IsMap, T, void*>&
    operator[](Key const& key)
        requires(MaybeMap && !MaybeMulti);

    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    std::conditional_t<IsMap, T, void*>&
    operator[](Key&& key)
        requires(MaybeMap && !MaybeMulti);

    //--------------------------------------------------------------------------
    //
    // Iterators
    //
    //--------------------------------------------------------------------------

    Iterator
    begin()
    {
        return Iterator(cont_.begin());
    }

    ConstIterator
    begin() const
    {
        return ConstIterator(cont_.begin());
    }

    ConstIterator
    cbegin() const
    {
        return ConstIterator(cont_.begin());
    }

    Iterator
    end()
    {
        return Iterator(cont_.end());
    }

    ConstIterator
    end() const
    {
        return ConstIterator(cont_.end());
    }

    ConstIterator
    cend() const
    {
        return ConstIterator(cont_.end());
    }

    Iterator
    iteratorTo(ValueType& value)
    {
        static_assert(std::is_standard_layout_v<Element>, "must be standard layout");
        return cont_.iterator_to(*reinterpret_cast<Element*>(
            reinterpret_cast<uint8_t*>(&value) -
            ((std::size_t)std::addressof(((Element*)0)->member))));
    }

    ConstIterator
    iteratorTo(ValueType const& value) const
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

    SizeType
    size() const noexcept
    {
        return cont_.size();
    }

    SizeType
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
    insert(ValueType const& value) -> std::pair<Iterator, bool>
        requires(!MaybeMulti);

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    auto
    insert(ValueType const& value) -> Iterator
        requires MaybeMulti;

    // map, set
    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    auto
    insert(ValueType&& value) -> std::pair<Iterator, bool>
        requires(!MaybeMulti && !MaybeMap);

    // multimap, multiset
    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    auto
    insert(ValueType&& value) -> Iterator
        requires(MaybeMulti && !MaybeMap);

    // map, set
    template <bool MaybeMulti = IsMulti>
    Iterator
    insert(ConstIterator /*hint*/, ValueType const& value)
        requires(!MaybeMulti)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert(value).first;
    }

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    Iterator
    insert(ConstIterator /*hint*/, ValueType const& value)
        requires MaybeMulti
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert(value);
    }

    // map, set
    template <bool MaybeMulti = IsMulti>
    Iterator
    insert(ConstIterator /*hint*/, ValueType&& value)
        requires(!MaybeMulti)
    {
        // Hint is ignored but we provide the interface so
        // callers may use ordered and unordered interchangeably.
        return insert(std::move(value)).first;
    }

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    Iterator
    insert(ConstIterator /*hint*/, ValueType&& value)
        requires MaybeMulti
    {
        // VFALCO TODO The hint could be used to let
        //             the client order equal ranges
        return insert(std::move(value));
    }

    // map, multimap
    template <class P, bool MaybeMap = IsMap>
    std::conditional_t<IsMulti, Iterator, std::pair<Iterator, bool>>
    insert(P&& value)
        requires(MaybeMap && std::is_constructible_v<ValueType, P&&>)
    {
        return emplace(std::forward<P>(value));
    }

    // map, multimap
    template <class P, bool MaybeMap = IsMap>
    std::conditional_t<IsMulti, Iterator, std::pair<Iterator, bool>>
    insert(ConstIterator hint, P&& value)
        requires(MaybeMap && std::is_constructible_v<ValueType, P&&>)
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
    insert(std::initializer_list<ValueType> init)
    {
        insert(init.begin(), init.end());
    }

    // set, map
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplace(Args&&... args) -> std::pair<Iterator, bool>
        requires(!MaybeMulti);

    // multiset, multimap
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplace(Args&&... args) -> Iterator
        requires MaybeMulti;

    // set, map
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplaceHint(ConstIterator /*hint*/, Args&&... args) -> std::pair<Iterator, bool>
        requires(!MaybeMulti);

    // multiset, multimap
    template <bool MaybeMulti = IsMulti, class... Args>
    Iterator
    emplaceHint(ConstIterator /*hint*/, Args&&... args)
        requires MaybeMulti
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
    erase(K const& k) -> SizeType;

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
    touch(K const& k) -> SizeType;

    //--------------------------------------------------------------------------
    //
    // Lookup
    //
    //--------------------------------------------------------------------------

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    SizeType
    count(K const& k) const
    {
        return cont_.count(
            k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    Iterator
    find(K const& k)
    {
        return Iterator(
            cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    ConstIterator
    find(K const& k) const
    {
        return ConstIterator(
            cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<Iterator, Iterator>
    equalRange(K const& k)
    {
        auto const r(cont_.equal_range(
            k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
        return std::make_pair(Iterator(r.first), Iterator(r.second));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<ConstIterator, ConstIterator>
    equalRange(K const& k) const
    {
        auto const r(cont_.equal_range(
            k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
        return std::make_pair(ConstIterator(r.first), ConstIterator(r.second));
    }

    //--------------------------------------------------------------------------
    //
    // Bucket interface
    //
    //--------------------------------------------------------------------------

    LocalIterator
    begin(SizeType n)
    {
        return LocalIterator(cont_.begin(n));
    }

    ConstLocalIterator
    begin(SizeType n) const
    {
        return ConstLocalIterator(cont_.begin(n));
    }

    ConstLocalIterator
    cbegin(SizeType n) const
    {
        return ConstLocalIterator(cont_.begin(n));
    }

    LocalIterator
    end(SizeType n)
    {
        return LocalIterator(cont_.end(n));
    }

    ConstLocalIterator
    end(SizeType n) const
    {
        return ConstLocalIterator(cont_.end(n));
    }

    ConstLocalIterator
    cend(SizeType n) const
    {
        return ConstLocalIterator(cont_.end(n));
    }

    SizeType
    bucketCount() const
    {
        return cont_.bucket_count();
    }

    SizeType
    maxBucketCount() const
    {
        return buck_.maxBucketCount();
    }

    SizeType
    bucketSize(SizeType n) const
    {
        return cont_.bucket_size(n);
    }

    SizeType
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
    rehash(SizeType count)
    {
        count = std::max(count, SizeType(size() / maxLoadFactor()));
        buck_.rehash(count, cont_);
    }

    void
    reserve(SizeType count)
    {
        rehash(std::ceil(count / maxLoadFactor()));
    }

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    Hasher const&
    hashFunction() const
    {
        return config_.hashFunction();
    }

    KeyEq const&
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
    bool
    operator==(AgedUnorderedContainer<
               false,
               OtherIsMap,
               OtherKey,
               OtherT,
               OtherDuration,
               OtherHash,
               KeyEqual,
               OtherAllocator> const& other) const
        requires(!MaybeMulti);

    template <
        bool OtherIsMap,
        class OtherKey,
        class OtherT,
        class OtherDuration,
        class OtherHash,
        class OtherAllocator,
        bool MaybeMulti = IsMulti>
    bool
    operator==(AgedUnorderedContainer<
               true,
               OtherIsMap,
               OtherKey,
               OtherT,
               OtherDuration,
               OtherHash,
               KeyEqual,
               OtherAllocator> const& other) const
        requires MaybeMulti;

private:
    bool
    wouldExceed(SizeType additional) const
    {
        return size() + additional > bucketCount() * maxLoadFactor();
    }

    void
    maybeRehash(SizeType additional)
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
    insertUnchecked(ValueType const& value) -> std::pair<Iterator, bool>
        requires(!MaybeMulti);

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    auto
    insertUnchecked(ValueType const& value) -> Iterator
        requires MaybeMulti;

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
        ClockType::time_point const& now)
    {
        auto& e(*pos.iterator());
        e.when = now;
        chronological.list_.erase(chronological.list_.iterator_to(e));
        chronological.list_.push_back(e);
    }

    template <
        bool MaybePropagate = std::allocator_traits<Allocator>::propagate_on_container_swap::value>
    void
    swapData(AgedUnorderedContainer& other) noexcept
        requires MaybePropagate
    {
        std::swap(config_.hashFunction(), other.config_.hashFunction());
        std::swap(config_.keyEq(), other.config_.keyEq());
        std::swap(config_.alloc(), other.config_.alloc());
        std::swap(config_.clock, other.config_.clock);
    }

    template <
        bool MaybePropagate = std::allocator_traits<Allocator>::propagate_on_container_swap::value>
    void
    swapData(AgedUnorderedContainer& other) noexcept
        requires(!MaybePropagate)
    {
        std::swap(config_.hashFunction(), other.config_.hashFunction());
        std::swap(config_.keyEq(), other.config_.keyEq());
        std::swap(config_.clock, other.config_.clock);
    }

private:
    ConfigT config_;
    Buckets buck_;
    ContType mutable cont_;
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
    AgedUnorderedContainer(ClockType& clock)
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
    AgedUnorderedContainer(ClockType& clock, Hash const& hash)
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
    AgedUnorderedContainer(ClockType& clock, KeyEqual const& keyEq)
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
    AgedUnorderedContainer(ClockType& clock, Allocator const& alloc)
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
    AgedUnorderedContainer(ClockType& clock, Hash const& hash, KeyEqual const& keyEq)
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
    AgedUnorderedContainer(ClockType& clock, Hash const& hash, Allocator const& alloc)
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
    AgedUnorderedContainer(ClockType& clock, KeyEqual const& keyEq, Allocator const& alloc)
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
        ClockType& clock,
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
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock)
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
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock, Hash const& hash)
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
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock, KeyEqual const& keyEq)
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
    AgedUnorderedContainer(InputIt first, InputIt last, ClockType& clock, Allocator const& alloc)
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
        ClockType& clock,
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
        ClockType& clock,
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
        ClockType& clock,
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
        ClockType& clock,
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
    AgedUnorderedContainer(std::initializer_list<ValueType> init, ClockType& clock)
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
        std::initializer_list<ValueType> init,
        ClockType& clock,
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
        std::initializer_list<ValueType> init,
        ClockType& clock,
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
        std::initializer_list<ValueType> init,
        ClockType& clock,
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
        std::initializer_list<ValueType> init,
        ClockType& clock,
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
        std::initializer_list<ValueType> init,
        ClockType& clock,
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
        std::initializer_list<ValueType> init,
        ClockType& clock,
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
        std::initializer_list<ValueType> init,
        ClockType& clock,
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
        SizeType const n(other.size());
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
    SizeType const n(other.size());
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
    std::initializer_list<ValueType> init) -> AgedUnorderedContainer&
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
template <class K, bool MaybeMulti, bool MaybeMap>
std::conditional_t<IsMap, T, void*>&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::at(K const& k)
    requires(MaybeMap && !MaybeMulti)
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
template <class K, bool MaybeMulti, bool MaybeMap>
std::conditional<IsMap, T, void*>::type const&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::at(
    K const& k) const
    requires(MaybeMap && !MaybeMulti)
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
template <bool MaybeMulti, bool MaybeMap>
std::conditional_t<IsMap, T, void*>&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator[](
    Key const& key)
    requires(MaybeMap && !MaybeMulti)
{
    maybeRehash(1);
    typename ContType::insert_commit_data d;
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
template <bool MaybeMulti, bool MaybeMap>
std::conditional_t<IsMap, T, void*>&
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::operator[](
    Key&& key)
    requires(MaybeMap && !MaybeMulti)
{
    maybeRehash(1);
    typename ContType::insert_commit_data d;
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
    ValueType const& value) -> std::pair<Iterator, bool>
    requires(!MaybeMulti)
{
    maybeRehash(1);
    typename ContType::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(value), std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(value));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(Iterator(iter), true);
    }
    return std::make_pair(Iterator(result.first), false);
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
    ValueType const& value) -> Iterator
    requires MaybeMulti
{
    maybeRehash(1);
    Element* const p(newElement(value));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return Iterator(iter);
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
    ValueType&& value) -> std::pair<Iterator, bool>
    requires(!MaybeMulti && !MaybeMap)
{
    maybeRehash(1);
    typename ContType::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(value), std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(std::move(value)));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(Iterator(iter), true);
    }
    return std::make_pair(Iterator(result.first), false);
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
    ValueType&& value) -> Iterator
    requires(MaybeMulti && !MaybeMap)
{
    maybeRehash(1);
    Element* const p(newElement(std::move(value)));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return Iterator(iter);
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
AgedUnorderedContainer<IsMulti, IsMap, Key, T, Clock, Hash, KeyEqual, Allocator>::emplace(
    Args&&... args) -> std::pair<Iterator, bool>
    requires(!MaybeMulti)
{
    maybeRehash(1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    Element* const p(newElement(std::forward<Args>(args)...));
    auto const result(cont_.insert(*p));
    if (result.second)
    {
        chronological.list_.push_back(*p);
        return std::make_pair(Iterator(result.first), true);
    }
    deleteElement(p);
    return std::make_pair(Iterator(result.first), false);
}

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
    Args&&... args) -> Iterator
    requires MaybeMulti
{
    maybeRehash(1);
    Element* const p(newElement(std::forward<Args>(args)...));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return Iterator(iter);
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
    ConstIterator /*hint*/,
    Args&&... args) -> std::pair<Iterator, bool>
    requires(!MaybeMulti)
{
    maybeRehash(1);
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    Element* const p(newElement(std::forward<Args>(args)...));
    typename ContType::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(p->value),
        std::cref(config_.hashFunction()),
        std::cref(config_.keyValueEqual()),
        d));
    if (result.second)
    {
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(Iterator(iter), true);
    }
    deleteElement(p);
    return std::make_pair(Iterator(result.first), false);
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
    -> SizeType
{
    auto iter(cont_.find(k, std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual())));
    if (iter == cont_.end())
        return 0;
    SizeType n(0);
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
    -> SizeType
{
    auto const now(clock().now());
    SizeType n(0);
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
bool
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
    requires(!MaybeMulti)
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
bool
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
    requires MaybeMulti
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
    ValueType const& value) -> std::pair<Iterator, bool>
    requires(!MaybeMulti)
{
    typename ContType::insert_commit_data d;
    auto const result(cont_.insert_check(
        extract(value), std::cref(config_.hashFunction()), std::cref(config_.keyValueEqual()), d));
    if (result.second)
    {
        Element* const p(newElement(value));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(Iterator(iter), true);
    }
    return std::make_pair(Iterator(result.first), false);
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
    ValueType const& value) -> Iterator
    requires MaybeMulti
{
    Element* const p(newElement(value));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert(*p));
    return Iterator(iter);
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

/**
 * Expire aged container items past the specified age.
 */
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
