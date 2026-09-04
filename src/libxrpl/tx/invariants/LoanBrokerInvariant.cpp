#include <xrpl/tx/invariants/LoanBrokerInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>

namespace xrpl {

void
ValidLoanBroker::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    // Track LoanBroker deletions so finalize() can enforce:
    //   (a) only ttLOAN_BROKER_DELETE removes a broker
    //   (b) at most one broker is removed per transaction
    //   (c) DebtTotal and OwnerCount were zero before deletion
    // `before` is the pre-transaction state, which is what
    // LoanBrokerDelete::preclaim reads. Erased trust lines and MPTokens need no
    // special handling here: the `if (after)` branch below already records them.
    if (isDelete && before && before->getType() == ltLOAN_BROKER)
    {
        if (deletedBroker_)
        {
            multipleBrokerDeletions_ = true;
        }
        else
        {
            deletedBroker_ = before;
        }
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
    }
}

bool
ValidLoanBroker::goodZeroDirectory(ReadView const& view, SLE::ConstRef dir, beast::Journal const& j)
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
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Loan Brokers will not exist on ledger if the Lending Protocol amendment
    // is not enabled, so there's no need to check it.

    // Deletion invariants (featureLendingProtocolV1_1). At most one
    // LoanBroker may be removed per transaction, and only by
    // ttLOAN_BROKER_DELETE, and only when its pre-state OwnerCount is zero and
    // its pre-state DebtTotal is zero to the precision of the vault asset. The
    // DebtTotal check complements ValidLoan's
    // LoanBrokerDelete-must-not-touch-any-loan rule: even a broker that has
    // finished paying off every loan may still hold non-zero exposure until
    // its LoanBrokerCoverWithdraw settles, and neither state is safe to
    // delete.
    if (view.rules().enabled(featureLendingProtocolV1_1))
    {
        if (multipleBrokerDeletions_)
        {
            JLOG(j.fatal())
                << "Invariant failed: more than one Loan Broker deleted in a single transaction";
            return false;
        }
        if (deletedBroker_)
        {
            if (tx.getTxnType() != ttLOAN_BROKER_DELETE)
            {
                JLOG(j.fatal()) << "Invariant failed: " <<  //
                    "Loan Broker deleted by a transaction other than LoanBrokerDelete";
                return false;
            }
            // Mirror LoanBrokerDelete::preclaim, which accepts a DebtTotal
            // that rounds to zero at the vault's AssetsTotal scale rather than
            // requiring an exact zero. Requiring more here would turn a
            // transaction the transactor deliberately permits into an
            // invariant failure.
            if (auto const debtTotal = deletedBroker_->at(sfDebtTotal); debtTotal != beast::kZero)
            {
                // The erased broker is also collected in brokers_, and that
                // loop reports a missing vault, so no separate diagnostic is
                // needed here. Without a vault there is no scale to round at,
                // so the residue cannot be excused as dust.
                auto const vault = view.read(keylet::vault(deletedBroker_->at(sfVaultID)));
                if (!vault ||
                    roundToAsset(
                        Asset{vault->at(sfAsset)},
                        debtTotal,
                        getAssetsTotalScale(vault),
                        Number::RoundingMode::TowardsZero) != beast::kZero)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: Loan Broker deleted with non-zero debt total";
                    return false;
                }
            }
            if (deletedBroker_->at(sfOwnerCount) != 0)
            {
                JLOG(j.fatal())
                    << "Invariant failed: Loan Broker deleted with non-zero owner count";
                return false;
            }
        }
    }

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

        if (view.rules().enabled(fixCleanup3_1_3))
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
        return true;
    });
}

}  // namespace xrpl
