#pragma once

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/container/aged_container.h>
#include <xrpl/beast/container/detail/aged_associative_container.h>
#include <xrpl/beast/container/detail/aged_container_iterator.h>
#include <xrpl/beast/container/detail/empty_base_optimization.h>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/version.hpp>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

namespace beast {
namespace detail {

// Traits templates used to discern reverse_iterators, which are disallowed
// for mutating operations.
template <class It>
struct IsBoostReverseIterator : std::false_type
{
    explicit IsBoostReverseIterator() = default;
};

template <class It>
struct IsBoostReverseIterator<boost::intrusive::reverse_iterator<It>> : std::true_type
{
    explicit IsBoostReverseIterator() = default;
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
    class Allocator = std::allocator<std::conditional_t<IsMap, std::pair<Key const, T>, Key>>>
class AgedOrderedContainer
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
    using is_unordered = std::false_type;
    using is_multi = std::integral_constant<bool, IsMulti>;
    using is_map = std::integral_constant<bool, IsMap>;

private:
    static Key const&
    extract(value_type const& value)
    {
        return AgedAssociativeContainerExtractT<IsMap>()(value);
    }

    // VFALCO TODO hoist to remove template argument dependencies
    struct Element : boost::intrusive::set_base_hook<
                         boost::intrusive::link_mode<boost::intrusive::normal_link>>,
                     boost::intrusive::list_base_hook<
                         boost::intrusive::link_mode<boost::intrusive::normal_link>>
    {
        // Stash types here so the iterator doesn't
        // need to see the container declaration.
        struct Stashed
        {
            explicit Stashed() = default;

            using value_type = typename AgedOrderedContainer::value_type;
            using time_point = typename AgedOrderedContainer::time_point;
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

    // VFALCO TODO This should only be enabled for maps.
    class PairValueCompare : public Compare
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

        PairValueCompare() = default;

        PairValueCompare(PairValueCompare const& other) : Compare(other)
        {
        }

    private:
        friend AgedOrderedContainer;

        PairValueCompare(Compare const& compare) : Compare(compare)
        {
        }
    };

    // Compares value_type against element, used in insert_check
    // VFALCO TODO hoist to remove template argument dependencies
    class KeyValueCompare : public Compare
    {
    public:
        using first_argument = Key;
        using second_argument = Element;
        using result_type = bool;

        KeyValueCompare() = default;

        KeyValueCompare(Compare const& compare) : Compare(compare)
        {
        }

        bool
        operator()(Key const& k, Element const& e) const
        {
            return Compare::operator()(k, extract(e.value));
        }

        bool
        operator()(Element const& e, Key const& k) const
        {
            return Compare::operator()(extract(e.value), k);
        }

        bool
        operator()(Element const& x, Element const& y) const
        {
            return Compare::operator()(extract(x.value), extract(y.value));
        }

        Compare&
        compare()
        {
            return *this;
        }

        [[nodiscard]] Compare const&
        compare() const
        {
            return *this;
        }
    };

    using list_type = typename boost::intrusive::
        make_list<Element, boost::intrusive::constant_time_size<false>>::type;

    using cont_type = std::conditional_t<
        IsMulti,
        typename boost::intrusive::make_multiset<
            Element,
            boost::intrusive::constant_time_size<true>,
            boost::intrusive::compare<KeyValueCompare>>::type,
        typename boost::intrusive::make_set<
            Element,
            boost::intrusive::constant_time_size<true>,
            boost::intrusive::compare<KeyValueCompare>>::type>;

    using ElementAllocator =
        typename std::allocator_traits<Allocator>::template rebind_alloc<Element>;

    using ElementAllocatorTraits = std::allocator_traits<ElementAllocator>;

    class ConfigT : private KeyValueCompare,
                    public beast::detail::EmptyBaseOptimization<ElementAllocator>
    {
    public:
        explicit ConfigT(clock_type& clock) : clock(clock)
        {
        }

        ConfigT(clock_type& clock, Compare const& comp) : KeyValueCompare(comp), clock(clock)
        {
        }

        ConfigT(clock_type& clock, Allocator const& alloc)
            : beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc), clock(clock)
        {
        }

        ConfigT(clock_type& clock, Compare const& comp, Allocator const& alloc)
            : KeyValueCompare(comp)
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(clock)
        {
        }

        ConfigT(ConfigT const& other)
            : KeyValueCompare(other.keyCompare())
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(
                  ElementAllocatorTraits::select_on_container_copy_construction(other.alloc()))
            , clock(other.clock)
        {
        }

        ConfigT(ConfigT const& other, Allocator const& alloc)
            : KeyValueCompare(other.keyCompare())
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(other.clock)
        {
        }

        ConfigT(ConfigT&& other)
            : KeyValueCompare(std::move(other.keyCompare()))
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(std::move(
                  static_cast<beast::detail::EmptyBaseOptimization<ElementAllocator>&>(other)))
            , clock(other.clock)
        {
        }

        ConfigT(
            ConfigT&& other,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
            Allocator const& alloc)
            : KeyValueCompare(std::move(other.keyCompare()))
            , beast::detail::EmptyBaseOptimization<ElementAllocator>(alloc)
            , clock(other.clock)
        {
        }

        ConfigT&
        operator=(ConfigT const& other)
        {
            if (this != &other)
            {
                compare() = other.compare();
                alloc() = other.alloc();
                clock = other.clock;
            }
            return *this;
        }

        ConfigT&
        operator=(ConfigT&& other)
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

        [[nodiscard]] Compare const&
        compare() const
        {
            return KeyValueCompare::compare();
        }

        KeyValueCompare&
        keyCompare()
        {
            return *this;
        }

        [[nodiscard]] KeyValueCompare const&
        keyCompare() const
        {
            return *this;
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
    using key_compare = Compare;
    using value_compare = std::conditional_t<IsMap, PairValueCompare, Compare>;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;

    // A set iterator (IsMap==false) is always const
    // because the elements of a set are immutable.
    using iterator = beast::detail::AgedContainerIterator<!IsMap, typename cont_type::iterator>;
    using const_iterator = beast::detail::AgedContainerIterator<true, typename cont_type::iterator>;
    using reverse_iterator =
        beast::detail::AgedContainerIterator<!IsMap, typename cont_type::reverse_iterator>;
    using const_reverse_iterator =
        beast::detail::AgedContainerIterator<true, typename cont_type::reverse_iterator>;

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
        ChronologicalT() = default;

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

    private:
        friend class AgedOrderedContainer;
        list_type mutable list_;
    } chronological;

    //--------------------------------------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------------------------------------

    AgedOrderedContainer() = delete;

    explicit AgedOrderedContainer(clock_type& clock);

    AgedOrderedContainer(clock_type& clock, Compare const& comp);

    AgedOrderedContainer(clock_type& clock, Allocator const& alloc);

    AgedOrderedContainer(clock_type& clock, Compare const& comp, Allocator const& alloc);

    template <class InputIt>
    AgedOrderedContainer(InputIt first, InputIt last, clock_type& clock);

    template <class InputIt>
    AgedOrderedContainer(InputIt first, InputIt last, clock_type& clock, Compare const& comp);

    template <class InputIt>
    AgedOrderedContainer(InputIt first, InputIt last, clock_type& clock, Allocator const& alloc);

    template <class InputIt>
    AgedOrderedContainer(
        InputIt first,
        InputIt last,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc);

    AgedOrderedContainer(AgedOrderedContainer const& other);

    AgedOrderedContainer(AgedOrderedContainer const& other, Allocator const& alloc);

    AgedOrderedContainer(AgedOrderedContainer&& other);

    AgedOrderedContainer(
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
        AgedOrderedContainer&& other,
        Allocator const& alloc);

    AgedOrderedContainer(std::initializer_list<value_type> init, clock_type& clock);

    AgedOrderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Compare const& comp);

    AgedOrderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Allocator const& alloc);

    AgedOrderedContainer(
        std::initializer_list<value_type> init,
        clock_type& clock,
        Compare const& comp,
        Allocator const& alloc);

    ~AgedOrderedContainer();

    AgedOrderedContainer&
    operator=(AgedOrderedContainer const& other);

    AgedOrderedContainer&
    operator=(AgedOrderedContainer&& other);

    AgedOrderedContainer&
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

    reverse_iterator
    rbegin()
    {
        return reverse_iterator(cont_.rbegin());
    }

    const_reverse_iterator
    rbegin() const
    {
        return const_reverse_iterator(cont_.rbegin());
    }

    const_reverse_iterator
    crbegin() const
    {
        return const_reverse_iterator(cont_.rbegin());
    }

    reverse_iterator
    rend()
    {
        return reverse_iterator(cont_.rend());
    }

    const_reverse_iterator
    rend() const
    {
        return const_reverse_iterator(cont_.rend());
    }

    const_reverse_iterator
    crend() const
    {
        return const_reverse_iterator(cont_.rend());
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

    // set
    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    auto
    insert(value_type&& value)
        -> std::enable_if_t<!MaybeMulti && !MaybeMap, std::pair<iterator, bool>>;

    // multiset
    template <bool MaybeMulti = IsMulti, bool MaybeMap = IsMap>
    auto
    insert(value_type&& value) -> std::enable_if_t<MaybeMulti && !MaybeMap, iterator>;

    //---

    // map, set
    template <bool MaybeMulti = IsMulti>
    auto
    insert(const_iterator hint, value_type const& value) -> std::enable_if_t<!MaybeMulti, iterator>;

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    std::enable_if_t<MaybeMulti, iterator>
    insert(const_iterator /*hint*/, value_type const& value)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
        return insert(value);
    }

    // map, set
    template <bool MaybeMulti = IsMulti>
    auto
    insert(const_iterator hint, value_type&& value) -> std::enable_if_t<!MaybeMulti, iterator>;

    // multimap, multiset
    template <bool MaybeMulti = IsMulti>
    std::enable_if_t<MaybeMulti, iterator>
    insert(const_iterator /*hint*/, value_type&& value)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
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
        for (; first != last; ++first)
            insert(cend(), *first);
    }

    void
    insert(std::initializer_list<value_type> init)
    {
        insert(init.begin(), init.end());
    }

    // map, set
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplace(Args&&... args) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>;

    // multiset, multimap
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplace(Args&&... args) -> std::enable_if_t<MaybeMulti, iterator>;

    // map, set
    template <bool MaybeMulti = IsMulti, class... Args>
    auto
    emplaceHint(const_iterator hint, Args&&... args)
        -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>;

    // multiset, multimap
    template <bool MaybeMulti = IsMulti, class... Args>
    std::enable_if_t<MaybeMulti, iterator>
    emplaceHint(const_iterator /*hint*/, Args&&... args)
    {
        // VFALCO TODO Figure out how to utilize 'hint'
        return emplace<MaybeMulti>(std::forward<Args>(args)...);
    }

    // enable_if prevents erase (reverse_iterator pos) from compiling
    template <
        bool IsConst,
        class Iterator,
        class = std::enable_if_t<!IsBoostReverseIterator<Iterator>::value>>
    beast::detail::AgedContainerIterator<false, Iterator>
    erase(beast::detail::AgedContainerIterator<IsConst, Iterator> pos);

    // enable_if prevents erase (reverse_iterator first, reverse_iterator last)
    // from compiling
    template <
        bool IsConst,
        class Iterator,
        class = std::enable_if_t<!IsBoostReverseIterator<Iterator>::value>>
    beast::detail::AgedContainerIterator<false, Iterator>
    erase(
        beast::detail::AgedContainerIterator<IsConst, Iterator> first,
        beast::detail::AgedContainerIterator<IsConst, Iterator> last);

    template <class K>
    auto
    erase(K const& k) -> size_type;

    void
    swap(AgedOrderedContainer& other) noexcept;

    //--------------------------------------------------------------------------

    // enable_if prevents touch (reverse_iterator pos) from compiling
    template <
        bool IsConst,
        class Iterator,
        class = std::enable_if_t<!IsBoostReverseIterator<Iterator>::value>>
    void
    touch(beast::detail::AgedContainerIterator<IsConst, Iterator> pos)
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
        return cont_.count(k, std::cref(config_.keyCompare()));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    find(K const& k)
    {
        return iterator(cont_.find(k, std::cref(config_.keyCompare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    find(K const& k) const
    {
        return const_iterator(cont_.find(k, std::cref(config_.keyCompare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<iterator, iterator>
    equalRange(K const& k)
    {
        auto const r(cont_.equal_range(k, std::cref(config_.keyCompare())));
        return std::make_pair(iterator(r.first), iterator(r.second));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    std::pair<const_iterator, const_iterator>
    equalRange(K const& k) const
    {
        auto const r(cont_.equal_range(k, std::cref(config_.keyCompare())));
        return std::make_pair(const_iterator(r.first), const_iterator(r.second));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    lowerBound(K const& k)
    {
        return iterator(cont_.lower_bound(k, std::cref(config_.keyCompare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    lowerBound(K const& k) const
    {
        return const_iterator(cont_.lower_bound(k, std::cref(config_.keyCompare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    iterator
    upperBound(K const& k)
    {
        return iterator(cont_.upper_bound(k, std::cref(config_.keyCompare())));
    }

    // VFALCO TODO Respect is_transparent (c++14)
    template <class K>
    const_iterator
    upperBound(K const& k) const
    {
        return const_iterator(cont_.upper_bound(k, std::cref(config_.keyCompare())));
    }

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    key_compare
    keyComp() const
    {
        return config_.compare();
    }

    // VFALCO TODO Should this return const reference for set?
    value_compare
    valueComp() const
    {
        return value_compare(config_.compare());
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
    operator==(AgedOrderedContainer<
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
    operator!=(AgedOrderedContainer<
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
    operator<(AgedOrderedContainer<
              OtherIsMulti,
              OtherIsMap,
              Key,
              OtherT,
              OtherDuration,
              Compare,
              OtherAllocator> const& other) const
    {
        value_compare const comp(valueComp());
        return std::lexicographical_compare(cbegin(), cend(), other.cbegin(), other.cend(), comp);
    }

    template <
        bool OtherIsMulti,
        bool OtherIsMap,
        class OtherT,
        class OtherDuration,
        class OtherAllocator>
    bool
    operator<=(AgedOrderedContainer<
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
    operator>(AgedOrderedContainer<
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
    operator>=(AgedOrderedContainer<
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
        bool IsConst,
        class Iterator,
        class = std::enable_if_t<!IsBoostReverseIterator<Iterator>::value>>
    void
    touch(
        beast::detail::AgedContainerIterator<IsConst, Iterator> pos,
        typename clock_type::time_point const& now);

    template <
        bool MaybePropagate = std::allocator_traits<Allocator>::propagate_on_container_swap::value>
    std::enable_if_t<MaybePropagate>
    swapData(AgedOrderedContainer& other) noexcept;

    template <
        bool MaybePropagate = std::allocator_traits<Allocator>::propagate_on_container_swap::value>
    std::enable_if_t<!MaybePropagate>
    swapData(AgedOrderedContainer& other) noexcept;

private:
    ConfigT config_;
    cont_type mutable cont_;
};

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    clock_type& clock)
    : config_(clock)
{
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    clock_type& clock,
    Compare const& comp)
    : config_(clock, comp), cont_(comp)
{
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    clock_type& clock,
    Allocator const& alloc)
    : config_(clock, alloc)
{
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    clock_type& clock,
    Compare const& comp,
    Allocator const& alloc)
    : config_(clock, comp, alloc), cont_(comp)
{
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class InputIt>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    InputIt first,
    InputIt last,
    clock_type& clock)
    : config_(clock)
{
    insert(first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class InputIt>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    InputIt first,
    InputIt last,
    clock_type& clock,
    Compare const& comp)
    : config_(clock, comp), cont_(comp)
{
    insert(first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class InputIt>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    InputIt first,
    InputIt last,
    clock_type& clock,
    Allocator const& alloc)
    : config_(clock, alloc)
{
    insert(first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class InputIt>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    InputIt first,
    InputIt last,
    clock_type& clock,
    Compare const& comp,
    Allocator const& alloc)
    : config_(clock, comp, alloc), cont_(comp)
{
    insert(first, last);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    AgedOrderedContainer const& other)
    : config_(other.config_)
#if BOOST_VERSION >= 108000
    , cont_(other.cont_.get_comp())
#else
    , cont_(other.cont_.comp())
#endif
{
    insert(other.cbegin(), other.cend());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    AgedOrderedContainer const& other,
    Allocator const& alloc)
    : config_(other.config_, alloc)
#if BOOST_VERSION >= 108000
    , cont_(other.cont_.get_comp())
#else
    , cont_(other.cont_.comp())
#endif
{
    insert(other.cbegin(), other.cend());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    AgedOrderedContainer&& other)
    : config_(std::move(other.config_)), cont_(std::move(other.cont_))
{
    chronological.list_ = std::move(other.chronological.list_);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    AgedOrderedContainer&& other,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    Allocator const& alloc)
    : config_(std::move(other.config_), alloc)
#if BOOST_VERSION >= 108000
    , cont_(std::move(other.cont_.get_comp()))
#else
    , cont_(std::move(other.cont_.comp()))
#endif

{
    insert(other.cbegin(), other.cend());
    other.clear();
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    std::initializer_list<value_type> init,
    clock_type& clock)
    : config_(clock)
{
    insert(init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    std::initializer_list<value_type> init,
    clock_type& clock,
    Compare const& comp)
    : config_(clock, comp), cont_(comp)
{
    insert(init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    std::initializer_list<value_type> init,
    clock_type& clock,
    Allocator const& alloc)
    : config_(clock, alloc)
{
    insert(init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::AgedOrderedContainer(
    std::initializer_list<value_type> init,
    clock_type& clock,
    Compare const& comp,
    Allocator const& alloc)
    : config_(clock, comp, alloc), cont_(comp)
{
    insert(init.begin(), init.end());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::~AgedOrderedContainer()
{
    clear();
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::operator=(
    AgedOrderedContainer const& other) -> AgedOrderedContainer&
{
    if (this != &other)
    {
        clear();
        this->config_ = other.config_;
        insert(other.begin(), other.end());
    }
    return *this;
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::operator=(
    AgedOrderedContainer&& other) -> AgedOrderedContainer&
{
    clear();
    this->config_ = std::move(other.config_);
    insert(other.begin(), other.end());
    other.clear();
    return *this;
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::operator=(
    std::initializer_list<value_type> init) -> AgedOrderedContainer&
{
    clear();
    insert(init);
    return *this;
}

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class K, bool MaybeMulti, bool MaybeMap, class>
std::conditional_t<IsMap, T, void*>&
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::at(K const& k)
{
    auto const iter(cont_.find(k, std::cref(config_.keyCompare())));
    if (iter == cont_.end())
        throw std::out_of_range("key not found");
    return iter->value.second;
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class K, bool MaybeMulti, bool MaybeMap, class>
typename std::conditional<IsMap, T, void*>::type const&
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::at(K const& k) const
{
    auto const iter(cont_.find(k, std::cref(config_.keyCompare())));
    if (iter == cont_.end())
        throw std::out_of_range("key not found");
    return iter->value.second;
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti, bool MaybeMap, class>
std::conditional_t<IsMap, T, void*>&
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::operator[](Key const& key)
{
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(key, std::cref(config_.keyCompare()), d));
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

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti, bool MaybeMap, class>
std::conditional_t<IsMap, T, void*>&
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::operator[](Key&& key)
{
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(key, std::cref(config_.keyCompare()), d));
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

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
void
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::clear()
{
    for (auto iter(chronological.list_.begin()); iter != chronological.list_.end();)
        deleteElement(&*iter++);
    chronological.list_.clear();
    cont_.clear();
}

// map, set
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::insert(
    value_type const& value) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>
{
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(extract(value), std::cref(config_.keyCompare()), d));
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
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::insert(
    value_type const& value) -> std::enable_if_t<MaybeMulti, iterator>
{
    auto const before(cont_.upper_bound(extract(value), std::cref(config_.keyCompare())));
    Element* const p(newElement(value));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert_before(before, *p));
    return iterator(iter);
}

// set
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti, bool MaybeMap>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::insert(value_type&& value)
    -> std::enable_if_t<!MaybeMulti && !MaybeMap, std::pair<iterator, bool>>
{
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(extract(value), std::cref(config_.keyCompare()), d));
    if (result.second)
    {
        Element* const p(newElement(std::move(value)));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    return std::make_pair(iterator(result.first), false);
}

// multiset
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti, bool MaybeMap>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::insert(value_type&& value)
    -> std::enable_if_t<MaybeMulti && !MaybeMap, iterator>
{
    auto const before(cont_.upper_bound(extract(value), std::cref(config_.keyCompare())));
    Element* const p(newElement(std::move(value)));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert_before(before, *p));
    return iterator(iter);
}

//---

// map, set
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::insert(
    const_iterator hint,
    value_type const& value) -> std::enable_if_t<!MaybeMulti, iterator>
{
    typename cont_type::insert_commit_data d;
    auto const result(
        cont_.insert_check(hint.iterator(), extract(value), std::cref(config_.keyCompare()), d));
    if (result.second)
    {
        Element* const p(newElement(value));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return iterator(iter);
    }
    return iterator(result.first);
}

// map, set
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::insert(
    const_iterator hint,
    value_type&& value) -> std::enable_if_t<!MaybeMulti, iterator>
{
    typename cont_type::insert_commit_data d;
    auto const result(
        cont_.insert_check(hint.iterator(), extract(value), std::cref(config_.keyCompare()), d));
    if (result.second)
    {
        Element* const p(newElement(std::move(value)));
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return iterator(iter);
    }
    return iterator(result.first);
}

// map, set
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti, class... Args>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::emplace(Args&&... args)
    -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>
{
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    Element* const p(newElement(std::forward<Args>(args)...));
    typename cont_type::insert_commit_data d;
    auto const result(cont_.insert_check(extract(p->value), std::cref(config_.keyCompare()), d));
    if (result.second)
    {
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    deleteElement(p);
    return std::make_pair(iterator(result.first), false);
}

// multiset, multimap
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti, class... Args>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::emplace(Args&&... args)
    -> std::enable_if_t<MaybeMulti, iterator>
{
    Element* const p(newElement(std::forward<Args>(args)...));
    auto const before(cont_.upper_bound(extract(p->value), std::cref(config_.keyCompare())));
    chronological.list_.push_back(*p);
    auto const iter(cont_.insert_before(before, *p));
    return iterator(iter);
}

// map, set
template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybeMulti, class... Args>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::emplaceHint(
    const_iterator hint,
    Args&&... args) -> std::enable_if_t<!MaybeMulti, std::pair<iterator, bool>>
{
    // VFALCO NOTE Its unfortunate that we need to
    //             construct element here
    Element* const p(newElement(std::forward<Args>(args)...));
    typename cont_type::insert_commit_data d;
    auto const result(
        cont_.insert_check(hint.iterator(), extract(p->value), std::cref(config_.keyCompare()), d));
    if (result.second)
    {
        auto const iter(cont_.insert_commit(*p, d));
        chronological.list_.push_back(*p);
        return std::make_pair(iterator(iter), true);
    }
    deleteElement(p);
    return std::make_pair(iterator(result.first), false);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool IsConst, class Iterator, class>
beast::detail::AgedContainerIterator<false, Iterator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::erase(
    beast::detail::AgedContainerIterator<IsConst, Iterator> pos)
{
    unlinkAndDeleteElement(&*((pos++).iterator()));
    return beast::detail::AgedContainerIterator<false, Iterator>(pos.iterator());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool IsConst, class Iterator, class>
beast::detail::AgedContainerIterator<false, Iterator>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::erase(
    beast::detail::AgedContainerIterator<IsConst, Iterator> first,
    beast::detail::AgedContainerIterator<IsConst, Iterator> last)
{
    for (; first != last;)
        unlinkAndDeleteElement(&*((first++).iterator()));

    return beast::detail::AgedContainerIterator<false, Iterator>(first.iterator());
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class K>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::erase(K const& k)
    -> size_type
{
    auto iter(cont_.find(k, std::cref(config_.keyCompare())));
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

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
void
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::swap(
    AgedOrderedContainer& other) noexcept
{
    swapData(other);
    std::swap(chronological, other.chronological);
    std::swap(cont_, other.cont_);
}

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <class K>
auto
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::touch(K const& k)
    -> size_type
{
    auto const now(clock().now());
    size_type n(0);
    auto const range(equalRange(k));
    for (auto iter : range)
    {
        touch(iter, now);
        ++n;
    }
    return n;
}

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <
    bool OtherIsMulti,
    bool OtherIsMap,
    class OtherT,
    class OtherDuration,
    class OtherAllocator>
bool
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::operator==(
    AgedOrderedContainer<
        OtherIsMulti,
        OtherIsMap,
        Key,
        OtherT,
        OtherDuration,
        Compare,
        OtherAllocator> const& other) const
{
    using Other = AgedOrderedContainer<
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
        [&eq, &other](value_type const& lhs, typename Other::value_type const& rhs) {
            return eq(extract(lhs), other.extract(rhs));
        });
}

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool IsConst, class Iterator, class>
void
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::touch(
    beast::detail::AgedContainerIterator<IsConst, Iterator> pos,
    typename clock_type::time_point const& now)
{
    auto& e(*pos.iterator());
    e.when = now;
    chronological.list_.erase(chronological.list_.iterator_to(e));
    chronological.list_.push_back(e);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybePropagate>
std::enable_if_t<MaybePropagate>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::swapData(
    AgedOrderedContainer& other) noexcept
{
    std::swap(config_.keyCompare(), other.config_.keyCompare());
    std::swap(config_.alloc(), other.config_.alloc());
    std::swap(config_.clock, other.config_.clock);
}

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
template <bool MaybePropagate>
std::enable_if_t<!MaybePropagate>
AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>::swapData(
    AgedOrderedContainer& other) noexcept
{
    std::swap(config_.keyCompare(), other.config_.keyCompare());
    std::swap(config_.clock, other.config_.clock);
}

}  // namespace detail

//------------------------------------------------------------------------------

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
struct IsAgedContainer<
    beast::detail::AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>>
    : std::true_type
{
    explicit IsAgedContainer() = default;
};

// Free functions

template <bool IsMulti, bool IsMap, class Key, class T, class Clock, class Compare, class Allocator>
void
swap(
    beast::detail::AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>& lhs,
    beast::detail::AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>&
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
    class Compare,
    class Allocator,
    class Rep,
    class Period>
std::size_t
expire(
    detail::AgedOrderedContainer<IsMulti, IsMap, Key, T, Clock, Compare, Allocator>& c,
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
