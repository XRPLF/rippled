#include <xrpl/tx/invariants/RepoInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>  // IWYU pragma: keep
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl {

void
ValidRepo::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (after && after->getType() == ltREPO)
    {
        repos_.emplace_back(before, after);
        if (isDelete)
            deleted_ = true;
    }
}

bool
ValidRepo::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    // A repo leaves the ledger only by cancel, close or default.
    if (deleted_)
    {
        switch (tx.getTxnType())
        {
            case ttREPO_CANCEL:
            case ttREPO_CLOSE:
            case ttREPO_DEFAULT:
                break;
            default:
                JLOG(j.fatal()) << "Invariant failed: a repo was deleted by transaction type "
                                << tx.getTxnType();
                return false;
        }
    }

    // A repo cannot exist unless the amendment is enabled, so no separate
    // amendment check is needed here.
    for (auto const& [before, after] : repos_)
    {
        // The two parties must be distinct, or the seller would be paying
        // itself and no collateral would be at risk.
        if (after->getAccountID(sfAccount) == after->getAccountID(sfCounterparty))
        {
            JLOG(j.fatal()) << "Invariant failed: a repo's seller is its counterparty";
            return false;
        }

        STAmount const collateral = (*after)[sfCollateralAmount];
        STAmount const price = (*after)[sfPurchasePrice];

        if (collateral <= beast::kZero || price <= beast::kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: a repo's collateral or price is not positive";
            return false;
        }

        // Settling in the collateral asset would let the seller repurchase
        // with the very asset that is locked.
        if (collateral.asset() == price.asset())
        {
            JLOG(j.fatal()) << "Invariant failed: a repo's collateral and cash are the same asset";
            return false;
        }

        // The repurchase deadline must fall after the offer stops being
        // acceptable, or a repo could mature before it could be accepted.
        if ((*after)[sfMaturityDate] <= (*after)[sfExpiration])
        {
            JLOG(j.fatal()) << "Invariant failed: a repo matures before it expires";
            return false;
        }

        if (!before)
            continue;

        // Terms are struck once. Nothing after create may restate them.
        auto const sameAccount = [&](SField const& f) {
            return before->getAccountID(f) == after->getAccountID(f);
        };
        auto const sameU32 = [&](SField const& f) {
            return before->getFieldU32(f) == after->getFieldU32(f);
        };

        if (!sameAccount(sfAccount) || !sameAccount(sfCounterparty) ||
            (*before)[sfCollateralAmount] != collateral || (*before)[sfPurchasePrice] != price ||
            !sameU32(sfInterestRate) || !sameU32(sfMaturityDate) || !sameU32(sfExpiration) ||
            !sameU32(sfGracePeriod))
        {
            JLOG(j.fatal()) << "Invariant failed: a repo's terms changed after creation";
            return false;
        }

        // A repo moves from pending to active and never back.
        bool const wasActive = before->isFieldPresent(sfStartDate);
        bool const isActiveNow = after->isFieldPresent(sfStartDate);

        if (wasActive && !isActiveNow)
        {
            JLOG(j.fatal()) << "Invariant failed: an active repo reverted to pending";
            return false;
        }

        if (wasActive && isActiveNow &&
            before->getFieldU32(sfStartDate) != after->getFieldU32(sfStartDate))
        {
            JLOG(j.fatal()) << "Invariant failed: a repo's start date changed";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
