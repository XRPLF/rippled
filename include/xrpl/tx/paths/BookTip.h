#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <boost/container/flat_set.hpp>

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
    uint256 book_;
    uint256 end_;
    uint256 dir_;
    uint256 index_;
    SLE::pointer entry_;
    Quality quality_{};
    // When set, the next step() leaves the current offer on the book instead
    // of deleting it (used to skip a contingent offer without consuming it).
    bool keepCurrent_{false};
    // Offers kept on the book during this walk. The walk normally advances
    // by deleting the consumed tip; a kept offer is not deleted, so step()
    // must iterate past every kept entry to reach the rest of its directory.
    boost::container::flat_set<uint256> kept_;

public:
    /**
     * Create the iterator.
     */
    BookTip(ApplyView& view, Book const& book);

    /**
     * Keep the current offer on the book when advancing past it.
     */
    void
    keepCurrent()
    {
        keepCurrent_ = true;
        kept_.insert(index_);
    }

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

    /**
     * Erases the current offer and advance to the next offer.
     * Complexity: Constant
     * @return `true` if there is a next offer
     */
    bool
    step(beast::Journal j);
};

}  // namespace xrpl
