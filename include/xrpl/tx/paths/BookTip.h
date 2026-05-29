#pragma once

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Quality.h>

#include <cstddef>
#include <cstdint>
#include <vector>

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
    Book originalBook_;
    uint256 book_;
    uint256 end_;
    uint256 dir_;
    uint256 index_;
    std::shared_ptr<SLE> entry_;
    Quality quality_{};

    // Plan 9: when the order-book index supplies an ordered offer-key snapshot
    // for this book, iterate it instead of re-walking the SHAMap with succ()
    // per offer. `useCursor_` is decided on the first step; thereafter the two
    // paths are mutually exclusive for the iterator's lifetime.
    std::vector<uint256> cursor_;
    std::size_t cursorPos_{0};
    bool useCursor_{false};
    std::uint64_t lastCursorQuality_{0};

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
