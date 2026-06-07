#pragma once

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Quality.h>

namespace xrpl {

class Logs;

/** Iterates and consumes raw offers in an order book.
    Offers are presented from highest quality to lowest quality. This will
    return all offers present including missing, invalid, unfunded, etc.
*/
class BookTip
{
private:
    ApplyView& view_;
    bool valid_{false};
    uint256 book_;
    uint256 end_;
    uint256 dir_;
    uint256 index_;
    SLE::pointer entry_;
    Quality quality_{};

public:
    /** Create the iterator. */
    BookTip(ApplyView& view, Book const& book);

    [[nodiscard]] uint256 const&
    dir() const noexcept
    {
        return dir_;
    }

    [[nodiscard]] uint256 const&
    index() const noexcept
    {
        return index_;
    }

    [[nodiscard]] Quality const&
    quality() const noexcept
    {
        return quality_;
    }

    [[nodiscard]] SLE::pointer const&
    entry() const noexcept
    {
        return entry_;
    }

    /** Erases the current offer and advance to the next offer.
        Complexity: Constant
        @return `true` if there is a next offer
    */
    bool
    step(beast::Journal j);
};

}  // namespace xrpl
