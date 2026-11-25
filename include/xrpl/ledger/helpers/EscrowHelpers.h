#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

/**
 * Validate that @p account may lock @p amount of a token for later delivery
 * to @p dest.
 *
 * The lock-side counterpart of escrowUnlockPreclaimHelper: every issuer
 * control (locking opt-in, authorization, freeze/lock, transferability,
 * spendable balance) that gates locking token value lives here, so any
 * transactor that locks funds applies the same rules. The signature is
 * view-based rather than PreclaimContext-based so it can also run from
 * doApply.
 */
template <ValidIssueType T>
TER
escrowLockPreclaimHelper(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal j);

template <>
TER
escrowLockPreclaimHelper<Issue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal j);

template <>
TER
escrowLockPreclaimHelper<MPTIssue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal j);

template <ValidIssueType T>
TER
escrowLockApplyHelper(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal);

template <>
TER
escrowLockApplyHelper<Issue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal);

template <>
TER
escrowLockApplyHelper<MPTIssue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal);

template <ValidIssueType T>
TER
escrowUnlockPreclaimHelper(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze = true);

template <>
TER
escrowUnlockPreclaimHelper<Issue>(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze);

template <>
TER
escrowUnlockPreclaimHelper<MPTIssue>(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze);

//------------------------------------------------------------------------------

template <ValidIssueType T>
TER
escrowUnlockApplyHelper(
    ApplyViewContext ctx,
    Rate lockedRate,
    SLE::Ref sleDest,
    XRPAmount xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

template <>
TER
escrowUnlockApplyHelper<Issue>(
    ApplyViewContext ctx,
    Rate lockedRate,
    SLE::Ref sleDest,
    XRPAmount xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

template <>
TER
escrowUnlockApplyHelper<MPTIssue>(
    ApplyViewContext ctx,
    Rate lockedRate,
    SLE::Ref sleDest,
    XRPAmount xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

/**
 * Smart escrow kill switches, driven by the voted FeeSettings.
 *
 * Zeroing `bytecodeSizeLimit` stops new uploads while leaving existing escrows
 * finishable without forcing holders to wait for `CancelAfter`.
 * Zeroing `gasLimit` stops both.
 */
/** @{ */
inline bool
isBytecodeUploadDisabled(Fees const& fees)
{
    return fees.bytecodeSizeLimit == 0 || fees.gasLimit == 0;
}

inline bool
isBytecodeExecutionDisabled(Fees const& fees)
{
    return fees.gasLimit == 0;
}
/** @} */

/**
 * Owner count an escrow costs.
 *
 *  An escrow costs one increment per started 500 bytes of bytecode, and one
 *  increment if it carries no bytecode.
 */
template <class T>
int32_t
calculateAdditionalReserve(T const& finishFunction)
{
    static auto constexpr kBytecodeReserveIncrement = 500;

    if (!finishFunction)
        return 1;

    auto const size = finishFunction->size();
    if (size == 0)
    {
        // LCOV_EXCL_START
        // Empty Bytecode is refused at create, so no escrow carries it. Ceiling
        // division would answer 0, which would subtract less than the create added.
        UNREACHABLE("xrpl::calculateAdditionalReserve : empty Bytecode");
        return 1;
        // LCOV_EXCL_STOP
    }

    return static_cast<int32_t>((size + kBytecodeReserveIncrement - 1) / kBytecodeReserveIncrement);
}

}  // namespace xrpl
