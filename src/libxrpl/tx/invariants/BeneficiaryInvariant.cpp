#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheck.h>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace xrpl {

void
ValidBeneficiary::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    // `after` is always present (on deletion it holds the final state), so it
    // alone determines the entry type.
    if (!after)
        return;  // LCOV_EXCL_LINE

    switch (after->getType())
    {
        case ltBENEFICIARY:
            entries_.emplace_back(before, after);
            if (isDelete)
            {
                designationRemoved_.insert((*after)[sfAccount]);
                deleted_ = true;
            }
            else if (!before)
            {
                designationAdded_.insert((*after)[sfAccount]);
            }
            break;

        // BeneficiarySet must not change a signer list, regular key or lsfDisableMaster.
        case ltSIGNER_LIST:
            keySettingsChanged_ = true;
            break;

        case ltACCOUNT_ROOT: {
            if (before &&
                ((*before)[~sfRegularKey] != (*after)[~sfRegularKey] ||
                 before->isFlag(lsfDisableMaster) != after->isFlag(lsfDisableMaster)))
            {
                keySettingsChanged_ = true;
            }

            // The timestamp lives on the AccountRoot, so its comings and goings
            // are tracked separately and compared against the entries at the end.
            auto const wasStamped =
                before ? (*before)[~sfLastInteraction] : std::optional<std::uint32_t>{};
            auto const isStamped =
                isDelete ? std::optional<std::uint32_t>{} : (*after)[~sfLastInteraction];

            if (!wasStamped && isStamped)
            {
                stampAdded_.insert((*after)[sfAccount]);
            }
            else if (wasStamped && !isStamped)
            {
                stampRemoved_.insert((*after)[sfAccount]);
            }
            else if (wasStamped && isStamped < wasStamped)
            {
                stampWentBackwards_ = true;
            }
            break;
        }

        default:
            break;
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

    if (keySettingsChanged_ && tx.getTxnType() == ttBENEFICIARY_SET)
    {
        JLOG(j.fatal()) << "Invariant failed: a beneficiary designation changed an account's key "
                           "settings";
        return false;
    }

    // Every created or modified entry names another account, has an in-range
    // time lock, and keeps the account it was created for.
    auto const wellFormed = [&j](auto const& entry) {
        auto const& [before, after] = entry;
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

        return true;
    };

    return std::ranges::all_of(entries_, wellFormed);
}

}  // namespace xrpl
