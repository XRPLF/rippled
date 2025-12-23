#ifndef XRPL_LEDGER_BOOK_DIRS_H_INCLUDED
#define XRPL_LEDGER_BOOK_DIRS_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>

namespace ripple {

class BookDirs
{
private:
    ReadView const* view_ = nullptr;
    uint256 const root_;
    uint256 const next_quality_;
    uint256 const key_;
    std::shared_ptr<SLE const> sle_ = nullptr;
    unsigned int entry_ = 0;
    uint256 index_;

public:
    class const_iterator;
    using value_type = std::shared_ptr<SLE const>;

    BookDirs(ReadView const&, Book const&);

    const_iterator
    begin() const;

    const_iterator
    end() const;
};

class BookDirs::const_iterator
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

    const_iterator(
        ReadView const& view,
        uint256 const& root,
        uint256 const& dir_key)
        : view_(&view), root_(root), key_(dir_key), cur_key_(dir_key)
    {
    }

    ReadView const* view_ = nullptr;
    uint256 root_;
    uint256 next_quality_;
    uint256 key_;
    uint256 cur_key_;
    std::shared_ptr<SLE const> sle_;
    unsigned int entry_ = 0;
    uint256 index_;
    std::optional<value_type> mutable cache_;

    static beast::Journal j_;
};

}  // namespace ripple

#endif
