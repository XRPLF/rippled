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
template <class ValueType>
class ReadViewFwdIter
{
public:
    using base_type = ReadViewFwdIter;

    using value_type = ValueType;

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

    [[nodiscard]] virtual value_type
    dereference() const = 0;
};

// A range using type-erased ForwardIterator
//
template <class ValueType>
class ReadViewFwdRange
{
public:
    using iter_base = ReadViewFwdIter<ValueType>;

    static_assert(
        std::is_nothrow_move_constructible<ValueType>{},
        "ReadViewFwdRange move and move assign constructors should be "
        "noexcept");

    class Iterator
    {
    public:
        using value_type = ValueType;

        using pointer = value_type const*;

        using reference = value_type const&;

        using difference_type = std::ptrdiff_t;

        using iterator_category = std::forward_iterator_tag;

        Iterator() = default;

        Iterator(Iterator const& other);
        Iterator(Iterator&& other) noexcept;

        // Used by the implementation
        explicit Iterator(ReadView const* view, std::unique_ptr<iter_base> impl);

        Iterator&
        operator=(Iterator const& other);

        Iterator&
        operator=(Iterator&& other) noexcept;

        bool
        operator==(Iterator const& other) const;

        bool
        operator!=(Iterator const& other) const;

        // Can throw
        reference
        operator*() const;

        // Can throw
        pointer
        operator->() const;

        Iterator&
        operator++();

        Iterator
        operator++(int);

    private:
        ReadView const* view_ = nullptr;
        std::unique_ptr<iter_base> impl_{};
        std::optional<value_type> mutable cache_;
    };

    static_assert(std::is_nothrow_move_constructible<Iterator>{}, "");
    static_assert(std::is_nothrow_move_assignable<Iterator>{}, "");

    using const_iterator = Iterator;

    using value_type = ValueType;

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
