#include <xrpl/tx/invariants/PermissionedDomainInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <vector>

namespace xrpl {

void
ValidPermissionedDomain::visitEntry(
    bool isDel,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && before->getType() != ltPERMISSIONED_DOMAIN)
        return;
    if (after && after->getType() != ltPERMISSIONED_DOMAIN)
        return;

    auto check = [isDel](std::vector<SleStatus>& sleStatus, std::shared_ptr<SLE const> const& sle) {
        auto const& credentials = sle->getFieldArray(sfAcceptedCredentials);
        auto const sorted = credentials::makeSorted(credentials);

        SleStatus ss{
            .credentialsSize = credentials.size(),
            .isSorted = false,
            .isUnique = !sorted.empty(),
            .isDelete = isDel};

        // If array have duplicates then all the other checks are invalid
        if (ss.isUnique)
        {
            unsigned i = 0;
            for (auto const& cred : sorted)
            {
                auto const& credTx = credentials[i++];
                ss.isSorted =
                    (cred.first == credTx[sfIssuer]) && (cred.second == credTx[sfCredentialType]);
                if (!ss.isSorted)
                    break;
            }
        }
        sleStatus.emplace_back(ss);
    };

    if (after)
        check(sleStatus_, after);
}

bool
ValidPermissionedDomain::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    auto check = [](SleStatus const& sleStatus, beast::Journal const& j) {
        if (!sleStatus.credentialsSize)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain with "
                               "no rules.";
            return false;
        }

        if (sleStatus.credentialsSize > kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain bad "
                               "credentials size "
                            << sleStatus.credentialsSize;
            return false;
        }

        if (!sleStatus.isUnique)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain credentials "
                               "aren't unique";
            return false;
        }

        if (!sleStatus.isSorted)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain credentials "
                               "aren't sorted";
            return false;
        }

        return true;
    };

    if (view.rules().enabled(fixCleanup3_1_3))
    {
        // No permissioned domains should be affected if the transaction failed
        if (!isTesSuccess(result))
        {
            // If nothing changed, all is good. If there were changes, that's bad.
            return sleStatus_.empty();
        }

        if (sleStatus_.size() > 1)
        {
            JLOG(j.fatal()) << "Invariant failed: transaction affected more "
                               "than 1 permissioned domain entry.";
            return false;
        }

        switch (tx.getTxnType())
        {
            case ttPERMISSIONED_DOMAIN_SET: {
                if (sleStatus_.empty())
                {
                    JLOG(j.fatal()) << "Invariant failed: no domain objects affected by "
                                       "PermissionedDomainSet";
                    return false;
                }

                auto const& sleStatus = sleStatus_[0];
                if (sleStatus.isDelete)
                {
                    JLOG(j.fatal()) << "Invariant failed: domain object "
                                       "deleted by PermissionedDomainSet";
                    return false;
                }
                return check(sleStatus, j);
            }
            case ttPERMISSIONED_DOMAIN_DELETE: {
                if (sleStatus_.empty())
                {
                    JLOG(j.fatal()) << "Invariant failed: no domain objects affected by "
                                       "PermissionedDomainDelete";
                    return false;
                }

                if (!sleStatus_[0].isDelete)
                {
                    JLOG(j.fatal()) << "Invariant failed: domain object "
                                       "modified, but not deleted by "
                                       "PermissionedDomainDelete";
                    return false;
                }
                return true;
            }
            default: {
                if (!sleStatus_.empty())
                {
                    JLOG(j.fatal()) << "Invariant failed: " << sleStatus_.size()
                                    << " domain object(s) affected by an "
                                       "unauthorized transaction. "
                                    << tx.getTxnType();
                    return false;
                }
                return true;
            }
        }
    }
    else
    {
        if (tx.getTxnType() != ttPERMISSIONED_DOMAIN_SET || !isTesSuccess(result) ||
            sleStatus_.empty())
            return true;
        return check(sleStatus_[0], j);
    }
}

}  // namespace xrpl
