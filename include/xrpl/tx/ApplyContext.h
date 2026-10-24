#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstddef>
#include <functional>
#include <optional>

namespace xrpl {

/**
 * State information when applying a tx.
 */
class ApplyContext
{
public:
    explicit ApplyContext(
        ServiceRegistry& registry,
        OpenView& base,
        std::optional<UInt256 const> const& parentBatchId,
        STTx const& tx,
        TER preclaimResult,
        XRPAmount baseFee,
        ApplyFlags flags,
        beast::Journal journal = beast::Journal{beast::Journal::getNullSink()});

    // Convenience constructor used only by tests that build an ApplyContext
    // directly (e.g. invariant checks). Production always uses the parentBatchId
    // constructor above; this one fixes parentBatchId to std::nullopt and so is
    // never valid for a batch inner (hence the TapBatch assert).
    explicit ApplyContext(
        ServiceRegistry& registry,
        OpenView& base,
        STTx const& tx,
        TER preclaimResult,
        XRPAmount baseFee,
        ApplyFlags flags,
        beast::Journal journal = beast::Journal{beast::Journal::getNullSink()})
        : ApplyContext(registry, base, std::nullopt, tx, preclaimResult, baseFee, flags, journal)
    {
        XRPL_ASSERT((flags & TapBatch) == 0, "Batch apply flag should not be set");
    }

    // viewRef_ points into view_, so a copied or moved context would alias the original.
    ApplyContext(ApplyContext const&) = delete;
    ApplyContext(ApplyContext&&) = delete;

    std::reference_wrapper<ServiceRegistry> registry;
    STTx const& tx;
    TER const preclaimResult;
    XRPAmount const baseFee;
    beast::Journal const journal;

    ApplyView&
    view()
    {
        return viewRef_.get();
    }

    [[nodiscard]] ApplyView const&
    view() const
    {
        return viewRef_.get();
    }

    // VFALCO Unfortunately this is necessary
    RawView&
    rawView()
    {
        return viewRef_.get();
    }

    [[nodiscard]] ApplyFlags const&
    flags() const
    {
        return flags_;
    }

    /**
     * Sets the DeliveredAmount field in the metadata
     */
    void
    deliver(STAmount const& amount)
    {
        viewRef_.get().deliver(amount);
    }

    /**
     * Discard changes and start fresh.
     */
    void
    discard();

    /**
     * Apply the transaction result to the base.
     */
    std::optional<TxMeta> apply(TER);

    /**
     * Get the number of unapplied changes.
     */
    std::size_t
    size();

    /**
     * Visit unapplied changes.
     */
    void
    visit(
        std::function<void(
            UInt256 const& key,
            bool isDelete,
            SLE::ConstRef before,
            SLE::ConstRef after)> const& func);

    void
    destroyXRP(XRPAmount const& fee)
    {
        viewRef_.get().rawDestroyXRP(fee);
    }

    /**
     * Registers a newly-created order book directory with the shared,
     * process-wide OrderBookDB, unless this transaction is being applied
     * under TapDryRun.
     */
    void
    addOrderBook(Book const& book);

    ApplyViewContext
    getApplyViewContext()
    {
        return {.view = viewRef_.get(), .tx = tx};
    }

private:
    OpenView& base_;
    ApplyFlags flags_;
    std::optional<ApplyViewImpl> view_;
    // Always refers to the engaged view_. Reseated whenever view_ is re-emplaced, so the
    // accessors need no optional check.
    std::reference_wrapper<ApplyViewImpl> viewRef_;

    // The ID of the batch transaction we are executing under, if set.
    std::optional<UInt256 const> parentBatchId_;
};

}  // namespace xrpl
