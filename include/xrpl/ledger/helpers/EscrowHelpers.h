#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

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

}  // namespace xrpl
