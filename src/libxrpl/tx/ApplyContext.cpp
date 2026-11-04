#include <xrpl/tx/ApplyContext.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstddef>
#include <functional>
#include <optional>

namespace xrpl {

ApplyContext::ApplyContext(
    ServiceRegistry& registry,
    OpenView& base,
    std::optional<UInt256 const> const& parentBatchId,
    STTx const& tx,
    TER preclaimResult,
    XRPAmount baseFee,
    ApplyFlags flags,
    beast::Journal journal)
    : registry(registry)
    , tx(tx)
    , preclaimResult(preclaimResult)
    , baseFee(baseFee)
    , journal(journal)
    , base_(base)
    , flags_(flags)
    , viewRef_(view_.emplace(&base_.view(), flags_))
    , parentBatchId_(parentBatchId)
{
    XRPL_ASSERT(
        parentBatchId.has_value() == ((flags_ & TapBatch) == TapBatch),
        "Parent Batch ID should be set if batch apply flag is set");
}

void
ApplyContext::discard()
{
    base_.discard();
    viewRef_ = view_.emplace(&base_.view(), flags_);
}

void
ApplyContext::finalize()
{
    base_.commit();
    viewRef_ = view_.emplace(&base_.view(), flags_);
}

std::optional<TxMeta>
ApplyContext::apply(TER ter)
{
    // tecINTERNAL reports an xrpld bug, not a result: nothing the VM recorded
    // before we hit it belongs in the metadata.
    if (ter != tecINTERNAL)
    {
        if (vmReturnCode_.has_value())
            viewRef_.get().setVMReturnCode(*vmReturnCode_);
        viewRef_.get().setGasUsed(gasUsed_);
    }
    // Only a successful call's events are recorded.
    if (contractEvents_.has_value() && isTesSuccess(ter))
        viewRef_.get().setContractEvents(*contractEvents_);
    return viewRef_.get().apply(
        base_.view(), tx, ter, parentBatchId_, (flags_ & TapDryRun) != 0u, journal);
}

void
ApplyContext::addOrderBook(Book const& book)
{
    if ((flags_ & TapDryRun) == TapNone)
        registry.get().getOrderBookDB().addOrderBook(book);
}

std::size_t
ApplyContext::size()
{
    return viewRef_.get().size();
}

void
ApplyContext::visit(
    std::function<void(UInt256 const&, bool, SLE::ConstRef, SLE::ConstRef)> const& func)
{
    viewRef_.get().visit(base_.view(), func);
}

}  // namespace xrpl
