#ifndef XRPL_LEDGER_APPLYVIEWIMPL_H_INCLUDED
#define XRPL_LEDGER_APPLYVIEWIMPL_H_INCLUDED

#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

namespace ripple {

/** Editable, discardable view that can build metadata for one tx.

    Iteration of the tx map is delegated to the base.

    @note Presented as ApplyView to clients.
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

    /** Apply the transaction.

        After a call to `apply`, the only valid
        operation on this object is to call the
        destructor.
    */
    std::optional<TxMeta>
    apply(
        OpenView& to,
        STTx const& tx,
        TER ter,
        std::optional<uint256> parentBatchId,
        bool isDryRun,
        beast::Journal j);

    /** Set the amount of currency delivered.

        This value is used when generating metadata
        for payments, to set the DeliveredAmount field.
        If the amount is not specified, the field is
        excluded from the resulting metadata.
    */
    void
    deliver(STAmount const& amount)
    {
        deliver_ = amount;
    }

    /** Get the number of modified entries
     */
    std::size_t
    size();

    /** Visit modified entries
     */
    void
    visit(
        OpenView& target,
        std::function<void(
            uint256 const& key,
            bool isDelete,
            std::shared_ptr<SLE const> const& before,
            std::shared_ptr<SLE const> const& after)> const& func);

private:
    std::optional<STAmount> deliver_;
};

}  // namespace ripple

#endif
