#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl {

void
ValidPermissionedDEX::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // Use `after` if present, otherwise fall back to `before` (defensive: handles
    // the case where `after` is null for a deleted entry, even though in practice
    // ApplyStateTable::visit always passes a non-null `after` for erased entries).
    auto const& sle = after ? after : before;

    if (sle && sle->getType() == ltDIR_NODE)
    {
        if (sle->isFieldPresent(sfDomainID))
            domains_.insert(sle->getFieldH256(sfDomainID));
    }

    if (sle && sle->getType() == ltOFFER)
    {
        if (sle->isFieldPresent(sfDomainID))
        {
            domains_.insert(sle->getFieldH256(sfDomainID));
        }
        else
        {
            regularOffers_ = true;
        }

        // if a hybrid offer is missing domain or additional book, there's
        // something wrong
        if (after && after->isFlag(lsfHybrid) &&
            (!after->isFieldPresent(sfDomainID) || !after->isFieldPresent(sfAdditionalBooks) ||
             after->getFieldArray(sfAdditionalBooks).size() > 1))
            badHybrids_ = true;
    }
}

bool
ValidPermissionedDEX::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    auto const txType = tx.getTxnType();
    if ((txType != ttPAYMENT && txType != ttOFFER_CREATE) || !isTesSuccess(result))
        return true;

    // For each offercreate transaction, check if
    // permissioned offers are valid
    if (txType == ttOFFER_CREATE && badHybrids_)
    {
        JLOG(j.fatal()) << "Invariant failed: hybrid offer is malformed";
        return false;
    }

    if (!tx.isFieldPresent(sfDomainID))
        return true;

    auto const domain = tx.getFieldH256(sfDomainID);

    if (!view.exists(keylet::permissionedDomain(domain)))
    {
        JLOG(j.fatal()) << "Invariant failed: domain doesn't exist";
        return false;
    }

    // for both payment and offercreate, there shouldn't be another domain
    // that's different from the domain specified
    for (auto const& d : domains_)
    {
        if (d != domain)
        {
            JLOG(j.fatal()) << "Invariant failed: transaction"
                               " consumed wrong domains";
            return false;
        }
    }

    if (regularOffers_)
    {
        JLOG(j.fatal()) << "Invariant failed: domain transaction"
                           " affected regular offers";
        return false;
    }

    return true;
}

}  // namespace xrpl
