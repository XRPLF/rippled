#include <xrpl/tx/invariants/TokenIssuanceInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/ledger/helpers/TokenIssuanceHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

namespace xrpl {

void
ValidTokenIssuance::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    auto const type = before ? before->getType() : after ? after->getType() : ltANY;
    if (type != ltTOKEN_ISSUANCE)
        return;

    if (!before && after)
    {
        ++created_;
        after_.push_back(after);
        return;
    }

    if (isDelete)
    {
        ++deleted_;
        deletedIssuances_.push_back(before);
        return;
    }

    if (before && after)
    {
        if (before->at(~sfMaximumAmount) != after->at(~sfMaximumAmount) ||
            before->at(sfTokenScale) != after->at(sfTokenScale) ||
            before->at(sfIssuer) != after->at(sfIssuer) ||
            before->at(sfCurrency) != after->at(sfCurrency) ||
            (before->isFieldPresent(sfMPTokenIssuanceID) &&
             before->at(~sfMPTokenIssuanceID) != after->at(~sfMPTokenIssuanceID)))
            immutableChanged_ = true;

        after_.push_back(after);
    }
}

bool
ValidTokenIssuance::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // No TokenIssuance can exist before the amendment, so any activity at
    // all pre-amendment is a hard failure.
    if (!view.rules().enabled(featureTokenIssuance) &&
        (created_ != 0 || deleted_ != 0 || !after_.empty() || !deletedIssuances_.empty()))
    {
        JLOG(j.fatal()) << "Invariant failed: TokenIssuance changed without amendment";
        return false;
    }

    // The destructive checks run regardless of the transaction result: a
    // failed transaction must not have created, deleted, or mutated
    // anything (and carries no privileges).
    if (created_ != 0 && !hasPrivilege(tx, Privilege::CreateTokenIssuance))
    {
        JLOG(j.fatal()) << "Invariant failed: TokenIssuance created without privilege";
        return false;
    }

    if (deleted_ != 0 && !hasPrivilege(tx, Privilege::DestroyTokenIssuance))
    {
        JLOG(j.fatal()) << "Invariant failed: TokenIssuance deleted without privilege";
        return false;
    }

    if (immutableChanged_)
    {
        JLOG(j.fatal()) << "Invariant failed: TokenIssuance immutable field changed";
        return false;
    }

    for (auto const& sle : deletedIssuances_)
    {
        if (Number const issued = sle->at(sfIssuedAmount); issued != Number{})
        {
            JLOG(j.fatal()) << "Invariant failed: TokenIssuance deleted with "
                               "outstanding IssuedAmount";
            return false;
        }
    }

    for (auto const& sle : after_)
    {
        if (tokenSupplyExceeded(view, sle))
        {
            JLOG(j.fatal()) << "Invariant failed: TokenIssuance supply cap exceeded";
            return false;
        }
    }

    if (!isTesSuccess(result))
        return true;

    // The privileged transactions must do exactly what they claim.
    if (hasPrivilege(tx, Privilege::CreateTokenIssuance) && (created_ != 1 || deleted_ != 0))
    {
        JLOG(j.fatal()) << "Invariant failed: TokenIssuanceCreate succeeded "
                           "without creating exactly one issuance";
        return false;
    }

    if (hasPrivilege(tx, Privilege::DestroyTokenIssuance) && (deleted_ != 1 || created_ != 0))
    {
        JLOG(j.fatal()) << "Invariant failed: TokenIssuanceDestroy succeeded "
                           "without deleting exactly one issuance";
        return false;
    }

    return true;
}

}  // namespace xrpl
