#include <xrpl/tx/invariants/PermissionedDomainInvariant.h>
//
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TxFormats.h>

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

        SleStatus ss{credentials.size(), false, !sorted.empty(), isDel};

        // If array have duplicates then all the other checks are invalid
        if (ss.isUnique_)
        {
            unsigned i = 0;
            for (auto const& cred : sorted)
            {
                auto const& credTx = credentials[i++];
                ss.isSorted_ =
                    (cred.first == credTx[sfIssuer]) && (cred.second == credTx[sfCredentialType]);
                if (!ss.isSorted_)
                    break;
            }
        }
        sleStatus.emplace_back(std::move(ss));
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
        if (!sleStatus.credentialsSize_)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain with "
                               "no rules.";
            return false;
        }

        if (sleStatus.credentialsSize_ > maxPermissionedDomainCredentialsArraySize)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain bad "
                               "credentials size "
                            << sleStatus.credentialsSize_;
            return false;
        }

        if (!sleStatus.isUnique_)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain credentials "
                               "aren't unique";
            return false;
        }

        if (!sleStatus.isSorted_)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain credentials "
                               "aren't sorted";
            return false;
        }

        return true;
    };

    if (view.rules().enabled(fixPermissionedDomainInvariant))
    {
        // No permissioned domains should be affected if the transaction failed
        if (result != tesSUCCESS)
            // If nothing changed, all is good. If there were changes, that's
            // bad.
            return sleStatus_.empty();

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
                if (sleStatus.isDelete_)
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

                if (!sleStatus_[0].isDelete_)
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
        if (tx.getTxnType() != ttPERMISSIONED_DOMAIN_SET || result != tesSUCCESS ||
            sleStatus_.empty())
            return true;
        return check(sleStatus_[0], j);
    }
}

}  // namespace xrpl
