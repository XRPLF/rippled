#pragma once

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl {

/** A class that simplifies iterating ledger directory pages

    The Dir class provides a forward iterator for walking through
    the uint256 values contained in ledger directories.

    The Dir class also allows accelerated directory walking by
    stepping directly from one page to the next using the next_page()
    member function.

    As of July 2024, the Dir class is only being used with NFTokenOffer
    directories and for unit tests.
*/
class Dir
{
private:
    ReadView const* view_ = nullptr;
    Keylet root_;
    SLE::const_pointer sle_;
    STVector256 const* indexes_ = nullptr;

public:
    class ConstIterator;
    using value_type = SLE::const_pointer;

    Dir(ReadView const&, Keylet const&);

    [[nodiscard]] ConstIterator
    begin() const;

    [[nodiscard]] ConstIterator
    end() const;
};

class Dir::ConstIterator
{
public:
    using value_type = Dir::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    bool
    operator==(ConstIterator const& other) const;

    bool
    operator!=(ConstIterator const& other) const
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

    ConstIterator&
    operator++();

    ConstIterator
    operator++(int);

    ConstIterator&
    nextPage();

    std::size_t
    pageSize();

    Keylet const&
    page() const
    {
        return page_;
    }

    uint256
    index() const
    {
        return index_;
    }

private:
    friend class Dir;

    ConstIterator(ReadView const& view, Keylet const& root, Keylet const& page)
        : view_(&view), root_(root), page_(page)
    {
    }

    ReadView const* view_ = nullptr;
    Keylet root_;
    Keylet page_;
    uint256 index_;
    std::optional<value_type> mutable cache_;
    SLE::const_pointer sle_;
    STVector256 const* indexes_ = nullptr;
    std::vector<uint256>::const_iterator it_;
};

}  // namespace xrpl
