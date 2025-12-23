#include <xrpl/ledger/ApplyViewImpl.h>

namespace ripple {

ApplyViewImpl::ApplyViewImpl(ReadView const* base, ApplyFlags flags)
    : ApplyViewBase(base, flags)
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
    return items_.apply(
        to,
        tx,
        ter,
        deliver_,
        parentBatchId,
        gasUsed_,
        wasmReturnCode_,
        isDryRun,
        j);
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

}  // namespace ripple
