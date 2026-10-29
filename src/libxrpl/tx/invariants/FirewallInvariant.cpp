#include <xrpl/tx/invariants/FirewallInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>

namespace xrpl {

void
ValidFirewall::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
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
    return std::ranges::all_of(firewalls_, [&j](auto const& entry) {
        auto const& [before, after] = entry;
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

        return true;
    });
}

}  // namespace xrpl
