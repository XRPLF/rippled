#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

void
ValidPermissionedDEX::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // Post-fixCleanup3_4_0: skip when after is null. Pre-amendment: fall back
    // to before so fully-consumed offers remain visible to the invariant.
    if (isFeatureEnabled(fixCleanup3_4_0) && !after)
        return;

    auto trackDomain = [this, isDelete](uint256 const& domain) {
        domainsOld_.insert(domain);
        if (!isDelete)
            domains_.insert(domain);
    };

    auto const sle = after ? after : before;

    if (sle && sle->getType() == ltDIR_NODE)
    {
        if (sle->isFieldPresent(sfDomainID))
            trackDomain(sle->getFieldH256(sfDomainID));
    }

    if (sle && sle->getType() == ltOFFER)
    {
        if (sle->isFieldPresent(sfDomainID))
        {
            trackDomain(sle->getFieldH256(sfDomainID));
        }
        else
        {
            regularOffersOld_ = true;
            if (!isDelete)
                regularOffers_ = true;
        }

        // pre-fixCleanup3_1_3: hybrid offer missing domain, missing
        // sfAdditionalBooks, or sfAdditionalBooks has more than one entry
        // `after` may be null when falling back to `before` for a deleted entry
        if (after && after->isFlag(lsfHybrid) &&
            (!after->isFieldPresent(sfDomainID) || !after->isFieldPresent(sfAdditionalBooks) ||
             after->getFieldArray(sfAdditionalBooks).size() > 1))
            badHybridsOld_ = true;

        // post-fixCleanup3_1_3: same as above but also catches size == 0
        if (after && after->isFlag(lsfHybrid) &&
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
    auto const& domains = view.rules().enabled(fixCleanup3_4_0) ? domains_ : domainsOld_;
    for (auto const& d : domains)
    {
        if (d != domain)
        {
            JLOG(j.fatal()) << "Invariant failed: transaction"
                               " consumed wrong domains";
            return false;
        }
    }

    bool const hasRegularOffers =
        view.rules().enabled(fixCleanup3_2_0) ? regularOffers_ : regularOffersOld_;
    if (hasRegularOffers)
    {
        JLOG(j.fatal()) << "Invariant failed: domain transaction"
                           " affected regular offers";
        return false;
    }

    return true;
}

}  // namespace xrpl
