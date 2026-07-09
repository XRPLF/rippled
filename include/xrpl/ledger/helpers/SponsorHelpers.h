#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <unordered_set>

namespace xrpl {

inline std::unordered_set<TxType> const kReserveSponsorAllowed = {
    // Explicitly allow-listed for v1.
    ttDELEGATE_SET,
    ttDEPOSIT_PREAUTH,
    ttPAYMENT,
    ttSIGNER_LIST_SET,
    ttCHECK_CANCEL,
    ttCHECK_CASH,
    ttCHECK_CREATE,
    ttESCROW_CANCEL,
    ttESCROW_CREATE,
    ttESCROW_FINISH,
    ttPAYCHAN_CLAIM,
    ttPAYCHAN_CREATE,
    ttPAYCHAN_FUND,
    ttCLAWBACK,
    ttMPTOKEN_AUTHORIZE,
    ttMPTOKEN_ISSUANCE_CREATE,
    ttMPTOKEN_ISSUANCE_DESTROY,
    ttMPTOKEN_ISSUANCE_SET,
    ttTRUST_SET,
    ttCREDENTIAL_ACCEPT,
    ttCREDENTIAL_CREATE,
    ttCREDENTIAL_DELETE,
    ttACCOUNT_SET,
    ttREGULAR_KEY_SET,
    ttSPONSORSHIP_TRANSFER,
};

inline bool
isFeeSponsored(STTx const& tx)
{
    return tx.isFieldPresent(sfSponsor) && ((tx.getFieldU32(sfSponsorFlags) & spfSponsorFee) != 0u);
}

inline bool
isReserveSponsored(STTx const& tx)
{
    return tx.isFieldPresent(sfSponsor) &&
        ((tx.getFieldU32(sfSponsorFlags) & spfSponsorReserve) != 0u);
}

std::optional<AccountID>
getTxReserveSponsorID(STTx const& tx);

std::expected<SLE::pointer, TER>
getTxReserveSponsor(ApplyViewContext ctx);

std::expected<SLE::const_pointer, TER>
getTxReserveSponsor(ReadView const& view, STTx const& tx);

/** The transaction's reserve sponsor for the given account, if applicable.
 *
 *  A reserve sponsor only covers the transaction submitter's own objects, so
 *  this returns the tx reserve sponsor SLE only when accountSle is the tx's own
 *  (non-pseudo) account; otherwise it returns a null sponsor pointer. This is
 *  the single source of truth for the "sponsor applies to tx.Account only" rule
 *  that the sponsor-deriving helper overloads in AccountRootHelpers rely on.
 *
 *  @param ctx The apply-view context (view + tx)
 *  @param accountSle The account whose sponsor is being resolved
 *  @return The sponsor SLE (nullptr if unsponsored), or tecINTERNAL if the
 *          sponsor account cannot be loaded (an already-checked invariant)
 */
[[nodiscard]] std::expected<SLE::pointer, TER>
getEffectiveTxReserveSponsor(ApplyViewContext ctx, SLE::const_ref accountSle);

std::optional<AccountID>
getLedgerEntryReserveSponsorID(SLE::const_ref sle, SF_ACCOUNT const& field = sfSponsor);

SLE::pointer
getLedgerEntryReserveSponsor(
    ApplyView& view,
    SLE::const_ref sle,
    SF_ACCOUNT const& field = sfSponsor);

void
addSponsorToLedgerEntry(
    SLE::ref sle,
    SLE::const_ref sponsorSle,
    SF_ACCOUNT const& field = sfSponsor);

/** Stamp the transaction's reserve sponsor onto a newly-created ledger entry.
 *
 *  Equivalent to the overload above, but resolves the sponsor via
 *  getTxReserveSponsor(ctx) instead of taking it explicitly. A no-op when the
 *  transaction is not reserve-sponsored. The entry is assumed to be owned by
 *  the transaction submitter, which is the only account a tx reserve sponsor
 *  can cover.
 */
void
addSponsorToLedgerEntry(ApplyViewContext ctx, SLE::ref sle, SF_ACCOUNT const& field = sfSponsor);

void
removeSponsorFromLedgerEntry(SLE::ref sle, SF_ACCOUNT const& field = sfSponsor);

std::optional<AccountID>
getLedgerEntryOwner(ReadView const& view, SLE const& sle, AccountID const& account);

bool
isLedgerEntrySupportedBySponsorship(SLE const& sle);

std::uint32_t
getLedgerEntryOwnerCount(SLE const& sle);

SF_ACCOUNT const&
getLedgerEntrySponsorField(SLE const& sle, AccountID const& owner);

}  // namespace xrpl
