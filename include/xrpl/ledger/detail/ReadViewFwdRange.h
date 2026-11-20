#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>

namespace xrpl {

class ReadView;

namespace detail {

// A type-erased ForwardIterator
//
template <class T>
class ReadViewFwdIter
{
public:
    using BaseType = ReadViewFwdIter;

    using ValueType = T;

    ReadViewFwdIter() = default;
    ReadViewFwdIter(ReadViewFwdIter const&) = default;
    ReadViewFwdIter&
    operator=(ReadViewFwdIter const&) = default;

    virtual ~ReadViewFwdIter() = default;

    [[nodiscard]] virtual std::unique_ptr<ReadViewFwdIter>
    copy() const = 0;

    [[nodiscard]] virtual bool
    equal(ReadViewFwdIter const& impl) const = 0;

    virtual void
    increment() = 0;

    [[nodiscard]] virtual ValueType
    dereference() const = 0;
};

// A range using type-erased ForwardIterator
//
template <class T>
class ReadViewFwdRange
{
public:
    using IterBase = ReadViewFwdIter<T>;

    static_assert(
        std::is_nothrow_move_constructible<T>{},
        "ReadViewFwdRange move and move assign constructors should be "
        "noexcept");

    class Iterator
    {
    public:
        using ValueType = T;
        using Pointer = ValueType const*;
        using Reference = ValueType const&;
        using DifferenceType = std::ptrdiff_t;
        using IteratorCategory = std::forward_iterator_tag;

        // Required by std::iterator_traits.
        // NOLINTBEGIN(readability-identifier-naming)
        using value_type = ValueType;
        using difference_type = DifferenceType;
        using iterator_category = IteratorCategory;
        // NOLINTEND(readability-identifier-naming)

        Iterator() = default;

        Iterator(Iterator const& other);
        Iterator(Iterator&& other) noexcept;

        // Used by the implementation
        explicit Iterator(ReadView const* view, std::unique_ptr<IterBase> impl);

        Iterator&
        operator=(Iterator const& other);

        Iterator&
        operator=(Iterator&& other) noexcept;

        bool
        operator==(Iterator const& other) const;

        // Can throw
        Reference
        operator*() const;

        // Can throw
        Pointer
        operator->() const;

        Iterator&
        operator++();

        Iterator
        operator++(int);

    private:
        ReadView const* view_ = nullptr;
        std::unique_ptr<IterBase> impl_{};
        std::optional<value_type> mutable cache_;
    };

    static_assert(std::is_nothrow_move_constructible<Iterator>{});
    static_assert(std::is_nothrow_move_assignable<Iterator>{});

    // Required by boost::range_const_iterator.
    // NOLINTNEXTLINE(readability-identifier-naming)
    using const_iterator = Iterator;

    using ValueType = T;

    ReadViewFwdRange() = delete;
    ReadViewFwdRange(ReadViewFwdRange const&) = default;
    ReadViewFwdRange&
    operator=(ReadViewFwdRange const&) = default;

    explicit ReadViewFwdRange(ReadView const& view) : view_(&view)
    {
    }

protected:
    ReadView const* view_;
};

}  // namespace detail
}  // namespace xrpl
