#include <xrpl/tx/invariants/PseudoAccountInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>

namespace xrpl {

void
ValidPseudoAccountOwnership::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (isDelete)
        return;

    // Only owner directory pages carry sfOwner; book directories don't.
    if (!after || after->getType() != ltDIR_NODE || !after->isFieldPresent(sfOwner))
        return;

    auto const& afterIndexes = after->getFieldV256(sfIndexes);
    if (afterIndexes.empty())
        return;

    auto& added = added_[after->getAccountID(sfOwner)];
    if (!before)
    {
        added.insert(added.end(), afterIndexes.begin(), afterIndexes.end());
        return;
    }

    auto const& beforeIndexes = before->getFieldV256(sfIndexes);
    for (auto const& index : afterIndexes)
    {
        if (std::ranges::find(beforeIndexes, index) == beforeIndexes.end())
            added.push_back(index);
    }
}

bool
ValidPseudoAccountOwnership::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!view.rules().enabled(fixPseudoAccountOwnership))
        return true;

    for (auto const& [owner, indexes] : added_)
    {
        auto const root = view.read(keylet::account(owner));
        if (!root || !isPseudoAccount(root))
            continue;

        // The object types each pseudo-account kind may own. A kind missing
        // here fails the check: adding a pseudo-account kind must add its
        // allowed set on purpose.
        auto const allowed = [&root]() -> std::optional<std::set<LedgerEntryType>> {
            if (root->isFieldPresent(sfAMMID))
                return std::set<LedgerEntryType>{ltAMM, ltRIPPLE_STATE, ltMPTOKEN};
            if (root->isFieldPresent(sfVaultID))
            {
                return std::set<LedgerEntryType>{
                    ltMPTOKEN_ISSUANCE, ltMPTOKEN, ltRIPPLE_STATE, ltLOAN_BROKER};
            }
            if (root->isFieldPresent(sfLoanBrokerID))
                return std::set<LedgerEntryType>{ltLOAN, ltMPTOKEN, ltRIPPLE_STATE};
            return std::nullopt;
        }();

        for (auto const& index : indexes)
        {
            auto const sle = view.read(keylet::unchecked(index));
            if (!sle)
            {
                // A directory entry without a ledger object is a dangling
                // link; that defect belongs to other invariants.
                continue;
            }

            if (!allowed || !allowed->contains(sle->getType()))
            {
                auto const item = LedgerFormats::getInstance().findByType(sle->getType());
                JLOG(j.fatal()) << "Invariant failed: pseudo-account " << toBase58(owner)
                                << " may not own an object of type "
                                << (item != nullptr ? item->getName()
                                                    : std::to_string(sle->getType()));
                return false;
            }
        }
    }

    return true;
}

}  // namespace xrpl
