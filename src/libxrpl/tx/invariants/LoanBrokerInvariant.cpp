/** @file
 *  Implements the `ValidLoanBroker` post-transaction invariant checker for
 *  the Lending Protocol (XLS-66). Verifies that every `LoanBroker` ledger
 *  object touched (directly or via its pseudo-account) by a transaction
 *  remains internally consistent: non-negative accounting fields, monotonic
 *  loan sequence, valid vault reference, and cover/balance agreement.
 */

#include <xrpl/tx/invariants/LoanBrokerInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>

namespace xrpl {

/** Collect ledger entries that may involve a LoanBroker.
 *
 *  Classifies each modified SLE into one of four buckets for deferred
 *  validation in `finalize`:
 *
 *  - `ltLOAN_BROKER` — stored in `brokers_` with before/after snapshots.
 *  - `ltACCOUNT_ROOT` carrying `sfLoanBrokerID` — the broker's pseudo-account;
 *    creates a placeholder `BrokerInfo{}` entry in `brokers_` if none exists,
 *    so the broker is checked even when its own SLE was not modified.
 *  - `ltRIPPLE_STATE` — appended to `lines_` for issuer lookup in `finalize`.
 *  - `ltMPTOKEN` — appended to `mpts_` for account lookup in `finalize`.
 *
 *  The `isDelete` flag is not used; the post-state `after` drives all checks
 *  except the sequence-monotonicity comparison, which uses both snapshots.
 *
 *  @param isDelete True if the entry is being deleted (unused in this checker).
 *  @param before   Pre-transaction SLE snapshot; may be null for new entries.
 *  @param after    Post-transaction SLE snapshot; may be null for deletions.
 */
void
ValidLoanBroker::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after)
    {
        if (after->getType() == ltLOAN_BROKER)
        {
            auto& broker = brokers_[after->key()];
            broker.brokerBefore = before;
            broker.brokerAfter = after;
        }
        else if (after->getType() == ltACCOUNT_ROOT && after->isFieldPresent(sfLoanBrokerID))
        {
            auto const& loanBrokerID = after->at(sfLoanBrokerID);
            // create an entry if one doesn't already exist
            brokers_.emplace(loanBrokerID, BrokerInfo{});
        }
        else if (after->getType() == ltRIPPLE_STATE)
        {
            lines_.emplace_back(after);
        }
        else if (after->getType() == ltMPTOKEN)
        {
            mpts_.emplace_back(after);
        }
    }
}

/** Validate the owner directory of a broker whose `sfOwnerCount` is zero.
 *
 *  Per XLS-66 §3.12.3, a broker with no outstanding obligations must have a
 *  single-page owner directory containing at most one entry, and that entry
 *  may only be an `ltRIPPLE_STATE` or `ltMPTOKEN` object — the trust line or
 *  MPToken through which the cover collateral is held.
 *
 *  @param view The current read-only ledger view.
 *  @param dir  The owner directory root SLE for the broker's pseudo-account.
 *  @param j    Journal for fatal-level diagnostics on failure.
 *  @return True if the directory satisfies the zero-owner-count constraint.
 */
bool
ValidLoanBroker::goodZeroDirectory(
    ReadView const& view,
    SLE::const_ref dir,
    beast::Journal const& j)
{
    auto const next = dir->at(~sfIndexNext);
    auto const prev = dir->at(~sfIndexPrevious);
    if ((prev && (*prev != 0u)) || (next && (*next != 0u)))
    {
        JLOG(j.fatal()) << "Invariant failed: Loan Broker with zero "
                           "OwnerCount has multiple directory pages";
        return false;
    }
    auto indexes = dir->getFieldV256(sfIndexes);
    if (indexes.size() > 1)
    {
        JLOG(j.fatal()) << "Invariant failed: Loan Broker with zero "
                           "OwnerCount has multiple indexes in the Directory root";
        return false;
    }
    if (indexes.size() == 1)
    {
        auto const index = indexes.value().front();
        auto const sle = view.read(keylet::unchecked(index));
        if (!sle)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker directory corrupt";
            return false;
        }
        if (sle->getType() != ltRIPPLE_STATE && sle->getType() != ltMPTOKEN)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker with zero "
                               "OwnerCount has an unexpected entry in the directory";
            return false;
        }
    }

    return true;
}

/** Validate all LoanBroker objects touched by the transaction.
 *
 *  First performs indirect broker discovery: iterates `lines_` and `mpts_`
 *  collected during `visitEntry`, reads each issuer/holder account root, and
 *  registers any account carrying `sfLoanBrokerID` in `brokers_`. This catches
 *  transactions that modify broker collateral without touching the
 *  `ltLOAN_BROKER` SLE directly.
 *
 *  For each discovered broker the following invariants are enforced:
 *  - `sfLoanSequence` must be monotonically non-decreasing (prevents loan-ID
 *    replay).
 *  - `sfDebtTotal` and `sfCoverAvailable` must be ≥ 0 (`STNumber` fields can
 *    represent negative values, which would indicate a bookkeeping bug).
 *  - `sfVaultID` must reference an existing `Vault` object.
 *  - `sfCoverAvailable` must not exceed the pseudo-account's on-ledger asset
 *    balance (`accountHolds` with freeze and auth both ignored, since
 *    pseudo-accounts are exempt from those controls).
 *  - Under `fixSecurity3_1_3`, `sfCoverAvailable` must equal the
 *    pseudo-account balance exactly, except during `ttLOAN_BROKER_DELETE`
 *    where the field is not zeroed before removal.
 *
 *  @note No amendment gate is needed here: `ltLOAN_BROKER` objects can only
 *      exist after the Lending Protocol amendment is enabled, so reaching this
 *      loop with live broker state implicitly confirms the amendment is active.
 *
 *  @param tx   The transaction that was applied.
 *  @param view The post-transaction ledger view.
 *  @param j    Journal for fatal-level diagnostics on failure.
 *  @return True if every tracked broker satisfies all invariants.
 */
bool
ValidLoanBroker::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // LoanBroker objects cannot exist unless the Lending Protocol amendment is
    // enabled, so there is no need to gate on it explicitly.

    for (auto const& line : lines_)
    {
        for (auto const& field : {&sfLowLimit, &sfHighLimit})
        {
            auto const account = view.read(keylet::account(line->at(*field).getIssuer()));
            // This Invariant doesn't know about the rules for Trust Lines, so
            // if the account is missing, don't treat it as an error. This
            // loop is only concerned with finding Broker pseudo-accounts
            if (account && account->isFieldPresent(sfLoanBrokerID))
            {
                auto const& loanBrokerID = account->at(sfLoanBrokerID);
                // create an entry if one doesn't already exist
                brokers_.emplace(loanBrokerID, BrokerInfo{});
            }
        }
    }
    for (auto const& mpt : mpts_)
    {
        auto const account = view.read(keylet::account(mpt->at(sfAccount)));
        // This Invariant doesn't know about the rules for MPTokens, so
        // if the account is missing, don't treat is as an error. This
        // loop is only concerned with finding Broker pseudo-accounts
        if (account && account->isFieldPresent(sfLoanBrokerID))
        {
            auto const& loanBrokerID = account->at(sfLoanBrokerID);
            // create an entry if one doesn't already exist
            brokers_.emplace(loanBrokerID, BrokerInfo{});
        }
    }

    for (auto const& [brokerID, broker] : brokers_)
    {
        auto const& after =
            broker.brokerAfter ? broker.brokerAfter : view.read(keylet::loanbroker(brokerID));

        if (!after)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker missing";
            return false;
        }

        auto const& before = broker.brokerBefore;

        // https://github.com/Tapanito/XRPL-Standards/blob/xls-66-lending-protocol/XLS-0066d-lending-protocol/README.md#3123-invariants
        // If `LoanBroker.OwnerCount = 0` the `DirectoryNode` will have at most
        // one node (the root), which will only hold entries for `RippleState`
        // or `MPToken` objects.
        if (after->at(sfOwnerCount) == 0)
        {
            auto const dir = view.read(keylet::ownerDir(after->at(sfAccount)));
            if (dir)
            {
                if (!goodZeroDirectory(view, dir, j))
                {
                    return false;
                }
            }
        }
        if (before && before->at(sfLoanSequence) > after->at(sfLoanSequence))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker sequence number "
                               "decreased";
            return false;
        }
        if (after->at(sfDebtTotal) < 0)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker debt total is negative";
            return false;
        }
        if (after->at(sfCoverAvailable) < 0)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker cover available is negative";
            return false;
        }
        auto const vault = view.read(keylet::vault(after->at(sfVaultID)));
        if (!vault)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker vault ID is invalid";
            return false;
        }
        auto const& vaultAsset = vault->at(sfAsset);
        auto const pseudoBalance = accountHolds(
            view,
            after->at(sfAccount),
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            j);
        if (after->at(sfCoverAvailable) < pseudoBalance)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker cover available "
                               "is less than pseudo-account asset balance";
            return false;
        }

        if (view.rules().enabled(fixSecurity3_1_3))
        {
            // Don't check the balance when LoanBroker is deleted,
            // sfCoverAvailable is not zeroed
            if (tx.getTxnType() != ttLOAN_BROKER_DELETE &&
                after->at(sfCoverAvailable) > pseudoBalance)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker cover available is greater "
                                   "than pseudo-account asset balance";
                return false;
            }
        }
    }
    return true;
}

}  // namespace xrpl
