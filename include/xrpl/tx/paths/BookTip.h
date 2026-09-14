#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

class Logs;

/**
 * Iterates and consumes raw offers in an order book.
 * Offers are presented from highest quality to lowest quality. This will
 * return all offers present including missing, invalid, unfunded, etc.
 */
class BookTip
{
private:
    ApplyView& view_;
    bool valid_{false};
    UInt256 book_;
    UInt256 end_;
    UInt256 dir_;
    UInt256 index_;
    SLE::pointer entry_;
    Quality quality_{};

public:
    /**
     * Create the iterator.
     */
    BookTip(ApplyView& view, Book const& book);

    [[nodiscard]] UInt256 const&
    dir() const noexcept
    {
        return dir_;
    }

    [[nodiscard]] UInt256 const&
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

    /**
     * Erases the current offer and advance to the next offer.
     * Complexity: Constant
     * @return `true` if there is a next offer
     */
    bool
    step(beast::Journal j);
};

}  // namespace xrpl
