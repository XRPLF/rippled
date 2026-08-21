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

#include <algorithm>

namespace xrpl {

void
ValidLoanBroker::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // The framework passes a non-null `after` even for deletions (it holds
    // the state at the time of erase), so distinguishing deletion from
    // modification requires isDelete rather than the presence of `after`.
    // Split deleted brokers into deletedBrokers_ - and skip the placeholder
    // emplace for deleted pseudo-accounts referencing them - so the live
    // check loop no longer has to exempt ttLOAN_BROKER_DELETE.
    if (isDelete)
    {
        if (before && before->getType() == ltLOAN_BROKER)
            deletedBrokers_.push_back(before);
        // Deleted trust lines / MPTokens for a broker's pseudo-account can no
        // longer meaningfully point at a live broker; there is nothing to
        // cross-check against, so ignore them here.
        return;
    }
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

        // Snapshot balance-bearing entries so broker-deletion checks can
        // compute the change in the owner's vault-asset balance. Non-mutually
        // exclusive with the branches above - an ACCOUNT_ROOT with an
        // sfLoanBrokerID is both a broker pseudo-account reference and a
        // balance holder.
        if (after->getType() == ltACCOUNT_ROOT || after->getType() == ltRIPPLE_STATE ||
            after->getType() == ltMPTOKEN)
        {
            touchedBalances_[after->key()] = {before, after};
        }
    }
}

Number
ValidLoanBroker::balanceOf(SLE::const_ref sle, AccountID const& id, Asset const& asset)
{
    if (!sle)
        return Number{};

    return std::visit(
        [&]<typename TIss>(TIss const& issue) -> Number {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                if (isXRP(issue))
                    return static_cast<std::int64_t>(sle->getFieldAmount(sfBalance).xrp().drops());
                // Trust-line balance is stored from the low-account's
                // perspective; flip the sign when @p id is the high account so
                // the result is in @p id's terms.
                auto bal = Number{sle->getFieldAmount(sfBalance)};
                if (id > issue.getIssuer())
                    bal = -bal;
                return bal;
            }
            else if constexpr (std::is_same_v<TIss, MPTIssue>)
            {
                return Number{static_cast<std::int64_t>(sle->getFieldU64(sfMPTAmount))};
            }
        },
        asset.value());
}

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

bool
ValidLoanBroker::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    // Loan Brokers will not exist on ledger if the Lending Protocol amendment
    // is not enabled, so there's no need to check it.

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

    // Item 29 (XLS-65 §3.4, XLS-66 §3.4): VaultDelete requires the vault's
    // pseudo-account owner directory to be empty; a broker referencing the
    // vault would still be on that directory, so a successful VaultDelete
    // implies no broker was touched. The pseudo-account owner-directory
    // residual check in ValidVault enforces this indirectly (item 9); this
    // check catches a compound transaction that would modify a broker while
    // deleting a vault. Class-2 (transaction post-condition); gate on
    // isTesSuccess.
    if (isTesSuccess(result) &&
        view.rules().enabled(fixCleanup3_4_0) &&
        tx.getTxnType() == ttVAULT_DELETE &&
        (!brokers_.empty() || !deletedBrokers_.empty()))
    {
        JLOG(j.fatal()) << "Invariant failed: VaultDelete must not touch any "
                           "loan broker";
        return false;
    }

    // Deletion-time preconditions (XLS-66 §3.1.5), stated positively over
    // deletedBrokers_ rather than by exempting the delete case from the live
    // check loop below. Gated on fixCleanup3_4_0 as new invariants.
    if (view.rules().enabled(fixCleanup3_4_0))
    {
        for (auto const& deletedBroker : deletedBrokers_)
        {
            // §3.1.5 precondition 1: all Loans associated with the LoanBroker
            // must be deleted first. The transactor checks this via preclaim
            // (tecHAS_OBLIGATIONS on OwnerCount != 0); re-asserting the
            // substantive property here catches an OwnerCount-tracking bug
            // that would defeat both the preclaim and this check together.
            // Class-1 (pure before-state fact), so no TER gate needed.
            if (deletedBroker->at(sfOwnerCount) != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: deleted LoanBroker must "
                                   "have no outstanding loans";
                return false;
            }

            // §3.1.5 precondition 3: first-loss capital must return to the
            // broker owner. Value conservation - what the pseudo-account
            // released must appear in the owner's balance. Class-2 (delta
            // check), so gate on isTesSuccess(result) to avoid firing against
            // the fee-claim-only state in InvariantScope::ProtocolOnly.
            if (!isTesSuccess(result))
                continue;

            auto const vaultSle = view.read(keylet::vault(deletedBroker->at(sfVaultID)));
            if (!vaultSle)
            {
                // The reverse "vault missing" case is caught by the live-broker
                // check loop below and by ValidVault; nothing useful to add
                // here.
                continue;
            }

            AccountID const owner = deletedBroker->at(sfOwner);
            Asset const vaultAsset = vaultSle->at(sfAsset);

            // Resolve the ledger key of the owner's asset holding: account
            // root for XRP, trust line for IOU, MPToken for MPT.
            uint256 const ownerKey = std::visit(
                [&]<typename TIss>(TIss const& issue) -> uint256 {
                    if constexpr (std::is_same_v<TIss, Issue>)
                    {
                        if (isXRP(issue))
                            return keylet::account(owner).key;
                        return keylet::trustLine(owner, issue).key;
                    }
                    else if constexpr (std::is_same_v<TIss, MPTIssue>)
                    {
                        return keylet::mptoken(issue.getMptID(), owner).key;
                    }
                },
                vaultAsset.value());

            Number const beforeCover = deletedBroker->at(sfCoverAvailable);

            auto const it = touchedBalances_.find(ownerKey);
            if (it == touchedBalances_.end())
            {
                // Owner's asset holding was not touched. Consistent only with
                // a zero cover return - if any capital was actually released,
                // it went nowhere the owner can see.
                if (beforeCover != Number{})
                {
                    JLOG(j.fatal())
                        << "Invariant failed: broker delete must return first-loss capital "
                           "to the owner";
                    return false;
                }
                continue;
            }

            auto const& [beforeSle, afterSle] = it->second;
            auto delta = balanceOf(afterSle, owner, vaultAsset) -
                balanceOf(beforeSle, owner, vaultAsset);

            // Fee-adjust: if the owner also paid the transaction fee (XRP
            // vaults only), their ACCOUNT_ROOT balance dropped by the fee in
            // addition to receiving the cover. Add the fee back so `delta`
            // reflects the vault-side flow alone.
            if (vaultAsset.native() && tx.getFeePayerID() == owner)
                delta += fee.drops();

            if (delta != beforeCover)
            {
                JLOG(j.fatal())
                    << "Invariant failed: broker delete must transfer first-loss capital "
                       "to the owner in full";
                return false;
            }
        }
    }

    return std::ranges::all_of(brokers_, [&](auto const& entry) {
        auto const& [brokerID, broker] = entry;
        auto const& after =
            broker.brokerAfter ? broker.brokerAfter : view.read(keylet::loanBroker(brokerID));

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

            // With no loans left the broker must carry no debt. LoanDelete
            // forgives sub-scale residues under XRPL_ASSERT_PARTS, which is
            // compiled out in release builds - so a live-network residual would
            // slip past the preclaim in LoanBrokerDelete and let a broker be
            // deleted while still owing the vault. Class-1 (pure after-state
            // implication); safe to place without a TER guard.
            if (view.rules().enabled(fixCleanup3_4_0) &&
                after->at(sfDebtTotal) != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker with zero "
                                   "OwnerCount must have zero DebtTotal";
                return false;
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

        // Item 16 (XLS-66 §3.7.1): reverse directory linkage. The broker
        // must appear in the vault pseudo-account's owner directory at page
        // LoanBroker.VaultNode. The forward Broker→Vault link is checked
        // above; this catches a bug that would leave the page pointer
        // pointing at a page that no longer references the broker.
        if (view.rules().enabled(fixCleanup3_4_0))
        {
            auto const dirPage = view.read(keylet::page(
                keylet::ownerDir(vault->at(sfAccount)),
                after->at(sfVaultNode)));
            if (!dirPage)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker VaultNode "
                                   "page does not exist in the vault "
                                   "pseudo-account owner directory";
                return false;
            }
            auto const& indexes = dirPage->getFieldV256(sfIndexes);
            if (std::find(indexes.begin(), indexes.end(), after->key()) ==
                indexes.end())
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker VaultNode "
                                   "page does not reference the broker";
                return false;
            }
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

        if (view.rules().enabled(fixCleanup3_1_3))
        {
            // Deleted brokers are handled separately in deletedBrokers_ above,
            // so the delete-case exemption that previously guarded this check
            // is no longer needed - a live broker whose CoverAvailable exceeds
            // its pseudo-account balance always indicates a real accounting
            // bug.
            if (after->at(sfCoverAvailable) > pseudoBalance)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker cover available is greater "
                                   "than pseudo-account asset balance";
                return false;
            }
        }

        if (view.rules().enabled(fixCleanup3_4_0))
        {
            // §3.1.10: DebtTotal must not exceed DebtMaximum. Currently only
            // enforced at LoanBrokerSet / LoanSet preclaim; making it universal
            // catches an accrual booking that would otherwise sneak through.
            // DebtMaximum == 0 disables the cap.
            if (after->at(sfDebtMaximum) != 0 &&
                after->at(sfDebtTotal) > after->at(sfDebtMaximum))
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker debt total exceeds "
                                   "debt maximum";
                return false;
            }

            // §3.1.2: rate bounds. Rates are immutable, so this is a
            // create-time hardening in practice, but cheap to make universal
            // so it also catches ledger-import / forced-mutation paths.
            // ManagementFeeRate is TenthBips16 (max 10 000); the two cover
            // rates are TenthBips32 (max 100 000). Minimum and liquidation
            // cover rates must both be zero or both non-zero (spec: the pair
            // together disables or enables first-loss capital).
            if (after->at(sfManagementFeeRate) > 10000)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker management fee rate "
                                   "out of range";
                return false;
            }
            if (after->at(sfCoverRateMinimum) > 100000 ||
                after->at(sfCoverRateLiquidation) > 100000)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker cover rate "
                                   "out of range";
                return false;
            }
            bool const minZero = after->at(sfCoverRateMinimum) == 0;
            bool const liqZero = after->at(sfCoverRateLiquidation) == 0;
            if (minZero != liqZero)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Broker cover rate minimum "
                                   "and liquidation must both be zero or both non-zero";
                return false;
            }

            // Δ OwnerCount == +1 on ttLOAN_SET, -1 on ttLOAN_DELETE, and
            // unchanged on every other transaction type that touches the
            // broker. Complements the LoanSequence monotonicity check above.
            // Class-2 (delta reasoning + tx type); gate on isTesSuccess.
            if (isTesSuccess(result) && before)
            {
                std::int64_t const beforeCount = before->at(sfOwnerCount);
                std::int64_t const afterCount = after->at(sfOwnerCount);
                std::int64_t const delta = afterCount - beforeCount;
                auto const expected = [&]() -> std::int64_t {
                    if (tx.getTxnType() == ttLOAN_SET)
                        return 1;
                    if (tx.getTxnType() == ttLOAN_DELETE)
                        return -1;
                    return 0;
                }();
                if (delta != expected)
                {
                    JLOG(j.fatal()) << "Invariant failed: Loan Broker owner count "
                                       "must change by +1 on LoanSet, -1 on LoanDelete, "
                                       "and be unchanged otherwise";
                    return false;
                }
            }

            // Item 24 (§3.11.5 fee routing): on ttLOAN_PAY the management fee
            // is routed to exactly one of {broker.Owner, broker.Account},
            // never both. LoanPay::doApply picks a single `brokerPayee` (line
            // 374) and sends the fee there via accountSendMulti - a bug that
            // credited both would silently succeed. Class-2 (delta reasoning
            // + tx type); gate on isTesSuccess.
            if (isTesSuccess(result) && tx.getTxnType() == ttLOAN_PAY)
            {
                Asset const asset = vault->at(sfAsset);
                auto const brokerDeltaFor = [&](AccountID const& id) -> Number {
                    uint256 const key = std::visit(
                        [&]<typename TIss>(TIss const& issue) -> uint256 {
                            if constexpr (std::is_same_v<TIss, Issue>)
                            {
                                if (isXRP(issue))
                                    return keylet::account(id).key;
                                return keylet::trustLine(id, issue).key;
                            }
                            else if constexpr (std::is_same_v<TIss, MPTIssue>)
                            {
                                return keylet::mptoken(issue.getMptID(), id).key;
                            }
                        },
                        asset.value());
                    auto const it = touchedBalances_.find(key);
                    if (it == touchedBalances_.end())
                        return Number{};
                    auto const& [b, a] = it->second;
                    return balanceOf(a, id, asset) - balanceOf(b, id, asset);
                };

                AccountID const brokerOwner = after->at(sfOwner);
                AccountID const brokerPseudo = after->at(sfAccount);
                Number ownerDelta = brokerDeltaFor(brokerOwner);
                Number const pseudoDelta = brokerDeltaFor(brokerPseudo);

                // Fee-adjust: XRP vault + brokerOwner is fee payer (the
                // self-loan edge case, when borrower == brokerOwner). Add the
                // fee back so `ownerDelta` reflects the vault-side flow alone,
                // matching the deletion-check convention above. The broker
                // pseudo-account can never be a fee payer.
                if (asset.native() && tx.getFeePayerID() == brokerOwner)
                    ownerDelta += fee.drops();

                if (ownerDelta > Number{} && pseudoDelta > Number{})
                {
                    JLOG(j.fatal()) << "Invariant failed: loan pay fee must be routed "
                                       "to the broker owner or the broker pseudo-account, "
                                       "not both";
                    return false;
                }
            }
        }
        return true;
    });
}

}  // namespace xrpl
