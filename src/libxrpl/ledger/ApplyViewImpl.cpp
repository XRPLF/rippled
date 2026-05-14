/** @file
 *  Implements `ApplyViewImpl`, the concrete per-transaction ledger-mutation
 *  view handed to every `Transactor` during the apply phase.
 *
 *  This file is intentionally minimal. All buffering logic lives in
 *  `ApplyStateTable` (see `detail/ApplyStateTable.h`); `ApplyViewImpl`
 *  contributes only the `deliver_` field (payment-delivery annotation for
 *  `TxMeta`) and the `apply()` commit boundary, which drains the buffer into
 *  a live `OpenView` and returns the generated `TxMeta`. After `apply()`
 *  returns the object must be destroyed — the buffer is drained and reuse
 *  would double-apply mutations or corrupt metadata.
 *
 *  @see ApplyViewImpl
 *  @see detail::ApplyStateTable
 *  @see detail::ApplyViewBase
 */
#include <xrpl/ledger/ApplyViewImpl.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

namespace xrpl {

ApplyViewImpl::ApplyViewImpl(ReadView const* base, ApplyFlags flags) : ApplyViewBase(base, flags)
{
}

std::optional<TxMeta>
ApplyViewImpl::apply(
    OpenView& to,
    STTx const& tx,
    TER ter,
    std::optional<uint256> parentBatchId,
    bool isDryRun,
    beast::Journal j)
{
    return items_.apply(to, tx, ter, deliver_, parentBatchId, isDryRun, j);
}

std::size_t
ApplyViewImpl::size()
{
    return items_.size();
}

void
ApplyViewImpl::visit(
    OpenView& to,
    std::function<void(
        uint256 const& key,
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after)> const& func)
{
    items_.visit(to, func);
}

}  // namespace xrpl
