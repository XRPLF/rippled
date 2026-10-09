#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstddef>
#include <iterator>
#include <optional>

namespace xrpl {

class BookDirs
{
private:
    ReadView const* view_ = nullptr;
    UInt256 const root_;
    UInt256 const nextQuality_;
    UInt256 const key_;
    SLE::ConstPointer sle_ = nullptr;
    unsigned int entry_ = 0;
    UInt256 index_;

public:
    class ConstIterator;
    using ValueType = SLE::ConstPointer;

    BookDirs(ReadView const&, Book const&);

    [[nodiscard]] ConstIterator
    begin() const;

    [[nodiscard]] ConstIterator
    end() const;
};

class BookDirs::ConstIterator
{
public:
    using ValueType = BookDirs::ValueType;
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

    ConstIterator() = default;

    bool
    operator==(ConstIterator const& other) const;

    Reference
    operator*() const;

    Pointer
    operator->() const
    {
        return &**this;
    }

    ConstIterator&
    operator++();

    ConstIterator
    operator++(int);

private:
    friend class BookDirs;

    ConstIterator(ReadView const& view, UInt256 const& root, UInt256 const& dirKey)
        : view_(&view), root_(root), key_(dirKey), curKey_(dirKey)
    {
    }

    ReadView const* view_ = nullptr;
    UInt256 root_;
    UInt256 nextQuality_;
    UInt256 key_;
    UInt256 curKey_;
    SLE::ConstPointer sle_;
    unsigned int entry_ = 0;
    UInt256 index_;
    std::optional<value_type> mutable cache_;
};

}  // namespace xrpl
