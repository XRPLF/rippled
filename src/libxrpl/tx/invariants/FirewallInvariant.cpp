#include <xrpl/tx/invariants/FirewallInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

void
ValidFirewall::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (after && after->getType() == ltFIREWALL)
        firewalls_.emplace_back(before, after);
}

bool
ValidFirewall::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    // A firewall cannot exist unless the amendment is enabled, so the amendment
    // needs no separate check here.
    for (auto const& [before, after] : firewalls_)
    {
        if (!after->isFieldPresent(sfOwner) || !after->isFieldPresent(sfCounterparty))
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall is missing its owner "
                               "or its counterparty";
            return false;
        }

        if (after->getAccountID(sfOwner) == after->getAccountID(sfCounterparty))
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall's counterparty is its owner";
            return false;
        }

        if (before && before->getAccountID(sfOwner) != after->getAccountID(sfOwner))
        {
            JLOG(j.fatal()) << "Invariant failed: a firewall's owner changed";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
