#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>

namespace xrpl {

void
ValidPermissionedDEX::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltDIR_NODE)
    {
        if (after->isFieldPresent(sfDomainID))
            domains_.insert(after->getFieldH256(sfDomainID));
    }

    if (after && after->getType() == ltOFFER)
    {
        if (after->isFieldPresent(sfDomainID))
        {
            domains_.insert(after->getFieldH256(sfDomainID));
        }
        else
        {
            regularOffers_ = true;
        }

        // pre-fixCleanup3_1_3: hybrid offer missing domain, missing
        // sfAdditionalBooks, or sfAdditionalBooks has more than one entry
        if (after->isFlag(lsfHybrid) &&
            (!after->isFieldPresent(sfDomainID) || !after->isFieldPresent(sfAdditionalBooks) ||
             after->getFieldArray(sfAdditionalBooks).size() > 1))
            badHybridsOld_ = true;

        // post-fixCleanup3_1_3: same as above but also catches size == 0
        if (after->isFlag(lsfHybrid) &&
            (!after->isFieldPresent(sfDomainID) || !after->isFieldPresent(sfAdditionalBooks) ||
             after->getFieldArray(sfAdditionalBooks).size() != 1))
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
    bool const isMalformed = view.rules().enabled(fixCleanup3_1_3) ? badHybrids_ : badHybridsOld_;
    if (txType == ttOFFER_CREATE && isMalformed)
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
