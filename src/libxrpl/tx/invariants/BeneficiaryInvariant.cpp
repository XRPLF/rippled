#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheck.h>

namespace xrpl {

void
ValidBeneficiary::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (before && before->getType() == ltBENEFICIARY)
    {
        if (isDelete)
        {
            designationRemoved_.insert((*before)[sfAccount]);
            deleted_ = true;
        }
    }

    if (after && after->getType() == ltBENEFICIARY)
    {
        entries_.emplace_back(before, after);
        if (!before)
            designationAdded_.insert((*after)[sfAccount]);
    }

    // The timestamp lives on the AccountRoot, so its comings and goings are
    // tracked separately and compared against the entries at the end.
    auto const stamp = [](SLE::const_ref sle) -> std::optional<std::uint32_t> {
        if (!sle || sle->getType() != ltACCOUNT_ROOT)
            return std::nullopt;
        return (*sle)[~sfLastInteraction];
    };

    if (after && after->getType() == ltACCOUNT_ROOT)
    {
        auto const wasStamped = before ? stamp(before) : std::nullopt;
        auto const isStamped = isDelete ? std::nullopt : stamp(after);

        if (!wasStamped && isStamped)
            stampAdded_.insert((*after)[sfAccount]);
        else if (wasStamped && !isStamped)
            stampRemoved_.insert((*after)[sfAccount]);
        else if (wasStamped && isStamped && *isStamped < *wasStamped)
            stampWentBackwards_ = true;
    }
}

bool
ValidBeneficiary::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (stampWentBackwards_)
    {
        JLOG(j.fatal()) << "Invariant failed: LastInteraction moved backwards";
        return false;
    }

    // A designation and its timestamp are created together and removed
    // together, so the two sets of accounts must match exactly.
    if (designationAdded_ != stampAdded_)
    {
        JLOG(j.fatal()) << "Invariant failed: a beneficiary designation was created without its "
                           "LastInteraction, or the reverse";
        return false;
    }

    if (designationRemoved_ != stampRemoved_)
    {
        JLOG(j.fatal()) << "Invariant failed: a beneficiary designation was removed without its "
                           "LastInteraction, or the reverse";
        return false;
    }

    if (deleted_)
    {
        switch (tx.getTxnType())
        {
            case ttBENEFICIARY_SET:
                break;
            default:
                JLOG(j.fatal())
                    << "Invariant failed: a beneficiary designation was deleted by transaction "
                       "type "
                    << tx.getTxnType();
                return false;
        }
    }

    for (auto const& [before, after] : entries_)
    {
        if ((*after)[sfAccount] == (*after)[sfBeneficiary])
        {
            JLOG(j.fatal()) << "Invariant failed: an account is its own beneficiary";
            return false;
        }

        auto const timeLock = (*after)[sfTimeLock];
        if (timeLock == 0 || timeLock > kMaxBeneficiaryTimeLock)
        {
            JLOG(j.fatal()) << "Invariant failed: the beneficiary time lock is out of range";
            return false;
        }

        if (before && (*before)[sfAccount] != (*after)[sfAccount])
        {
            JLOG(j.fatal()) << "Invariant failed: the beneficiary entry changed account";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
