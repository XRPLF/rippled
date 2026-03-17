#include <xrpl/ledger/SponsorHelpers.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

bool
isReserveSponsored(STTx const& tx)
{
    return tx.getFieldU32(sfSponsorFlags) & spfSponsorReserve;
}

bool
isSponsorReserveCoSigning(STTx const& tx)
{
    if (!tx.isFieldPresent(sfSponsorSignature))
        return false;
    return isReserveSponsored(tx);
}

std::optional<AccountID>
getTxReserveSponsorAccountID(STTx const& tx)
{
    if (tx.isFieldPresent(sfSponsor) && isReserveSponsored(tx))
    {
        return tx.getAccountID(sfSponsor);
    }
    return {};
}

std::optional<AccountID>
getLedgerEntryReserveSponsorAccountID(
    std::shared_ptr<SLE const> const& sle,
    SF_ACCOUNT const& field)
{
    if (sle->isFieldPresent(field))
        return sle->getAccountID(field);
    return {};
}

void
addSponsorToLedgerEntry(
    std::shared_ptr<SLE> const& sle,
    std::shared_ptr<SLE> const& sponsorSle,
    SF_ACCOUNT const& field)
{
    XRPL_ASSERT(
        (sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
            (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
        "addSponsorToLedgerEntry : Invalid field to the LedgerEntry");
    if (sponsorSle)
        sle->setAccountID(field, sponsorSle->getAccountID(sfAccount));
}

void
removeSponsorFromLedgerEntry(std::shared_ptr<SLE> const& sle, SF_ACCOUNT const& field)
{
    XRPL_ASSERT(
        (sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
            (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
        "removeSponsorFromLedgerEntry : Invalid field to the LedgerEntry");
    if (sle->isFieldPresent(field))
        sle->makeFieldAbsent(field);
}

}  // namespace xrpl
