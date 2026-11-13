#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <optional>

namespace xrpl {

template <typename ViewT>
class PayChannelEntry : public SLEBase<ViewT, ltPAYCHAN>
{
public:
    using Base = SLEBase<ViewT, ltPAYCHAN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit PayChannelEntry(
        AccountID const& src,
        AccountID const& dst,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::payChannel(src, dst, seq), view, j)
    {
    }

    /**
     * Determine whether a payment channel time field represents an expired
     * time.
     *
     * @param timeField  The optional expiry timestamp (seconds since the XRP
     *                   Ledger epoch). If empty, returns false.
     * @return           @c true if @p timeField is set and the indicated time
     *                   is in the past relative to the view's parent close
     *                   time; @c false otherwise.
     */
    [[nodiscard]] bool
    isExpired(std::optional<std::uint32_t> timeField) const
    {
        if (!timeField)
            return false;
        auto const& view = this->readView();
        if (view.rules().enabled(fixCleanup3_2_0))
            return after(view.header().parentCloseTime, *timeField);
        return view.header().parentCloseTime.time_since_epoch().count() >= *timeField;
    }

    /**
     * Closes this payment channel: returns its remaining funds to the
     * channel's source account, removes it from the source's and (if
     * present) the destination's owner directories, decrements the source's
     * owner count, and erases the channel from the ledger.
     *
     * @return tesSUCCESS on success; tefBAD_LEDGER if a directory removal
     *         fails; tefINTERNAL if the source account SLE cannot be found.
     */
    [[nodiscard]] TER
    removeFromLedger()
        requires Base::kIsWritable;
};

using PayChannelEntryR = PayChannelEntry<ReadView>;
using PayChannelEntryW = PayChannelEntry<ApplyView>;

}  // namespace xrpl
