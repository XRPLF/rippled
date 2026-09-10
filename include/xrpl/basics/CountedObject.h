#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <boost/core/demangle.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace xrpl {

/**
 * Manages all counted object types.
 *
 * Counters register themselves on a lock-free intrusive list maintained
 * by this object when constructed. Because counters are never destroyed
 * or removed, the ABA problem does not apply.
 *
 * The registry is iterable as a forward range.
 */
class CountedObjects
{
public:
    /**
     * Implementation for @ref CountedObject.
     *
     * @internal
     */
    class Counter
    {
    public:
        Counter(std::string name) noexcept;

        // Counters are intrusive list nodes whose addresses are published
        // in the registry; they must never be copied or moved. The atomic
        // members already force this, but we make it explicit.
        Counter(Counter const&) = delete;
        Counter&
        operator=(Counter const&) = delete;
        Counter(Counter&&) = delete;
        Counter&
        operator=(Counter&&) = delete;

        std::uint32_t
        increment() noexcept
        {
            auto const newCount = count_.fetch_add(1, std::memory_order::relaxed) + 1;
            XRPL_ASSERT(newCount != 0, "xrpl::CountedObjects::Counter::increment : no overflow");

            auto maxCount = maxCount_.load(std::memory_order::relaxed);

            while (newCount > maxCount &&
                   !maxCount_.compare_exchange_weak(maxCount, newCount, std::memory_order::relaxed))
            {
            }

            return newCount;
        }

        std::uint32_t
        decrement() noexcept
        {
            auto const prev = count_.fetch_sub(1, std::memory_order::relaxed);
            XRPL_ASSERT(prev != 0, "xrpl::CountedObjects::Counter::decrement : no underflow");
            return prev - 1;
        }

        [[nodiscard]] std::uint32_t
        count() const noexcept
        {
            return count_.load(std::memory_order::relaxed);
        }

        [[nodiscard]] std::uint32_t
        max() const noexcept
        {
            return std::max(
                count_.load(std::memory_order::relaxed),
                maxCount_.load(std::memory_order::relaxed));
        }

        [[nodiscard]] std::string const&
        name() const noexcept
        {
            return name_;
        }

    private:
        friend class CountedObjects;

        Counter* next_ = nullptr;
        std::atomic<std::uint32_t> count_ = 0;
        std::atomic<std::uint32_t> maxCount_ = 0;
        std::string const name_;
    };

    class Iterator
    {
    public:
        using value_type = Counter const;
        using reference = value_type&;
        using pointer = value_type*;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        explicit Iterator(Counter* c = nullptr) noexcept : current_(c)
        {
        }

        reference
        operator*() const noexcept
        {
            return *current_;
        }

        pointer
        operator->() const noexcept
        {
            return current_;
        }

        Iterator&
        operator++() noexcept
        {
            current_ = current_->next_;
            return *this;
        }

        Iterator
        operator++(int) noexcept
        {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        bool
        operator==(Iterator const&) const noexcept = default;

    private:
        Counter* current_;
    };

    constexpr CountedObjects() noexcept = default;

    [[nodiscard]] auto
    begin() const noexcept
    {
        return Iterator{head_.load(std::memory_order::acquire)};
    }

    [[nodiscard]] auto
    end() const noexcept
    {
        return Iterator{};
    }

private:
    std::atomic<Counter*> head_ = nullptr;
};

/** The global counted object registry. */
inline constinit CountedObjects gCountedObjects;

inline CountedObjects::Counter::Counter(std::string name) noexcept : name_(std::move(name))
{
    next_ = gCountedObjects.head_.load(std::memory_order::relaxed);

    while (!gCountedObjects.head_.compare_exchange_weak(
        next_, this, std::memory_order::release, std::memory_order::relaxed))
    {
    }
}

//------------------------------------------------------------------------------

/**
 * Tracks the number of instances of an object.
 *
 * Derived classes have their instances counted automatically. This is used
 * for reporting purposes.
 *
 * The constructors are private and `Object` is befriended so that the
 * CRTP parameter must be the deriving class itself: a copy-paste error
 * like `class B : public CountedObject<A>` fails to compile instead of
 * silently polluting A's count.
 *
 * @note This class has no move operations by design: a derived class's
 *       move constructor falls back to the copy constructor for this
 *       base, so the newly created instance is counted. This keeps the
 *       invariant that count is the number of outstanding subobjects.
 *
 * @warning Counted objects constructed during dynamic initialization of
 *          other translation units may have their increments discarded when
 *          counter itself is dynamically initialized. Do not create counted
 *          objects before main() begins.
 *
 * @ingroup basics
 */
template <class Object>
    requires std::is_class_v<Object>
class CountedObject
{
    static inline CountedObjects::Counter counter{boost::core::demangle(typeid(Object).name())};

    CountedObject() noexcept
    {
        counter.increment();
    }

    CountedObject(CountedObject const&) noexcept
    {
        counter.increment();
    }

    CountedObject&
    operator=(CountedObject const&) noexcept = default;

public:
    ~CountedObject() noexcept
    {
        counter.decrement();
    }

    friend Object;
};

// Ensure that CountedObject is an empty base class. This does not
// create an instance of CountedObject.
static_assert(std::is_empty_v<CountedObject<struct EboProbe>>);

}  // namespace xrpl
