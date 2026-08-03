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
    std::function<
        void(uint256 const& key, bool isDelete, SLE::const_ref before, SLE::const_ref after)> const&
        func)
{
    items_.visit(to, func);
}

}  // namespace xrpl
