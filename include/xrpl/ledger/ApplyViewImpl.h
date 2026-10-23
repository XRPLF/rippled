#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstddef>
#include <functional>
#include <optional>

namespace xrpl {

/**
 * Editable, discardable view that can build metadata for one tx.
 *
 * Iteration of the tx map is delegated to the base.
 *
 * @note Presented as ApplyView to clients.
 */
class ApplyViewImpl final : public detail::ApplyViewBase
{
public:
    ApplyViewImpl() = delete;
    ApplyViewImpl(ApplyViewImpl const&) = delete;
    ApplyViewImpl&
    operator=(ApplyViewImpl&&) = delete;
    ApplyViewImpl&
    operator=(ApplyViewImpl const&) = delete;

    ApplyViewImpl(ApplyViewImpl&&) = default;
    ApplyViewImpl(ReadView const* base, ApplyFlags flags);

    /**
     * Apply the transaction.
     *
     * The buffered changes stay in the view afterwards. The only valid
     * operations on this object after `apply` are `reset()`, which makes it
     * clean again, and the destructor.
     */
    std::optional<TxMeta>
    apply(
        OpenView& to,
        STTx const& tx,
        TER ter,
        std::optional<UInt256> parentBatchId,
        bool isDryRun,
        beast::Journal j);

    /**
     * Set the amount of currency delivered.
     *
     * This value is used when generating metadata
     * for payments, to set the DeliveredAmount field.
     * If the amount is not specified, the field is
     * excluded from the resulting metadata.
     */
    void
    deliver(STAmount const& amount)
    {
        deliver_ = amount;
    }

    /**
     * Discard every buffered change and the delivered amount.
     *
     * Afterwards the view is indistinguishable from one freshly constructed
     * over the same base with the same flags.
     */
    void
    reset();

    /**
     * Get the number of modified entries
     */
    std::size_t
    size();

    /**
     * Visit modified entries
     */
    void
    visit(
        OpenView& target,
        std::function<void(
            UInt256 const& key,
            bool isDelete,
            SLE::ConstRef before,
            SLE::ConstRef after)> const& func);

private:
    std::optional<STAmount> deliver_;
};

}  // namespace xrpl
