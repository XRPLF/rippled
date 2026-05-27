#pragma once

#include <xrpl/ledger/ReadView.h>

namespace xrpl {

class BookDirs
{
private:
    ReadView const* view_ = nullptr;
    uint256 const root_;
    uint256 const nextQuality_;
    uint256 const key_;
    std::shared_ptr<SLE const> sle_ = nullptr;
    unsigned int entry_ = 0;
    uint256 index_;

public:
    class const_iterator;  // NOLINT(readability-identifier-naming)
    using value_type = std::shared_ptr<SLE const>;

    BookDirs(ReadView const&, Book const&);

    [[nodiscard]] const_iterator
    begin() const;

    [[nodiscard]] const_iterator
    end() const;
};

class BookDirs::const_iterator  // NOLINT(readability-identifier-naming)
{
public:
    using value_type = BookDirs::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    const_iterator() = default;

    bool
    operator==(const_iterator const& other) const;

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const;

    pointer
    operator->() const
    {
        return &**this;
    }

    const_iterator&
    operator++();

    const_iterator
    operator++(int);

private:
    friend class BookDirs;

    const_iterator(ReadView const& view, uint256 const& root, uint256 const& dirKey)
        : view_(&view), root_(root), key_(dirKey), curKey_(dirKey)
    {
    }

    ReadView const* view_ = nullptr;
    uint256 root_;
    uint256 nextQuality_;
    uint256 key_;
    uint256 curKey_;
    std::shared_ptr<SLE const> sle_;
    unsigned int entry_ = 0;
    uint256 index_;
    std::optional<value_type> mutable cache_;
};

}  // namespace xrpl
