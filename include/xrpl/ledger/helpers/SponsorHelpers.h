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

namespace xrpl {

/**
 * Whether the given transaction type may use reserve sponsorship (v1).
 *
 * Reserve sponsorship is restricted to an explicit allow-list of transaction
 * types; all others reject spfSponsorReserve at preflight.
 */
bool
isReserveSponsorAllowed(TxType txType);

/**
 * Whether the transaction's fee is sponsored (sfSponsor present + spfSponsorFee set).
 */
inline bool
isFeeSponsored(STTx const& tx)
{
    return tx.isFieldPresent(sfSponsor) && ((tx.getFieldU32(sfSponsorFlags) & spfSponsorFee) != 0u);
}

/**
 * Whether the transaction's reserve is sponsored (sfSponsor present + spfSponsorReserve set).
 */
inline bool
isReserveSponsored(STTx const& tx)
{
    return tx.isFieldPresent(sfSponsor) &&
        ((tx.getFieldU32(sfSponsorFlags) & spfSponsorReserve) != 0u);
}

/**
 * Return the AccountID of the transaction's reserve sponsor, or nullopt if unsponsored.
 */
std::optional<AccountID>
getTxReserveSponsorID(STTx const& tx);

/**
 * Return a mutable SLE for the transaction's reserve sponsor account.
 *
 * @param ctx The apply-view context (view + tx)
 * @return The sponsor account SLE, a null pointer if the tx is not
 *         reserve-sponsored, or tecINTERNAL if the sponsor account cannot
 *         be loaded (an already-checked invariant).
 */
std::expected<SLE::pointer, TER>
getTxReserveSponsor(ApplyViewContext ctx);

/**
 * Return a read-only SLE for the transaction's reserve sponsor account.
 *
 * @param view The ledger read view
 * @param tx   The transaction to inspect
 * @return The sponsor account SLE, a null pointer if the tx is not
 *         reserve-sponsored, or tecINTERNAL if the sponsor account cannot
 *         be loaded (an already-checked invariant).
 */
std::expected<SLE::const_pointer, TER>
getTxReserveSponsor(ReadView const& view, STTx const& tx);

/**
 * The transaction's reserve sponsor for the given account, if applicable.
 *
 * A reserve sponsor only covers the transaction submitter's own objects, so
 * this returns the tx reserve sponsor SLE only when accountSle is the tx's own
 * (non-pseudo) account; otherwise it returns a null sponsor pointer. This is
 * the single source of truth for the "sponsor applies to tx.Account only" rule
 * that the sponsor-deriving helper overloads in AccountRootHelpers rely on.
 *
 * @param ctx The apply-view context (view + tx)
 * @param accountSle The account whose sponsor is being resolved
 * @return The sponsor SLE (nullptr if unsponsored), or tecINTERNAL if the
 *         sponsor account cannot be loaded (an already-checked invariant)
 */
[[nodiscard]] std::expected<SLE::pointer, TER>
getEffectiveTxReserveSponsor(ApplyViewContext ctx, SLE::const_ref accountSle);

/**
 * Return the AccountID stored in the given sponsor field of a ledger entry, or nullopt if absent.
 */
std::optional<AccountID>
getLedgerEntryReserveSponsorID(SLE::const_ref sle, SF_ACCOUNT const& field = sfSponsor);

/**
 * Return a mutable SLE for the reserve sponsor recorded on a ledger entry.
 *
 * Reads the sponsor AccountID from @p field on @p sle and peeks the
 * corresponding account root in @p view.
 *
 * @param view  The mutable apply view
 * @param sle   The ledger entry whose sponsor field is inspected
 * @param field The field that holds the sponsor AccountID (defaults to sfSponsor)
 * @return The sponsor account SLE, or a null pointer if the entry is unsponsored.
 */
SLE::pointer
getLedgerEntryReserveSponsor(
    ApplyView& view,
    SLE::const_ref sle,
    SF_ACCOUNT const& field = sfSponsor);

/**
 * Stamp a reserve sponsor onto a ledger entry using an explicit sponsor SLE.
 *
 * Sets @p field on @p sle to the AccountID from @p sponsorSle. A no-op when
 * @p sponsorSle is null (unsponsored). For RippleState entries the field must
 * be sfHighSponsor or sfLowSponsor; for all other entry types it must be
 * sfSponsor.
 *
 * @param sle       The ledger entry to stamp
 * @param sponsorSle The sponsor's account root SLE (null → no-op)
 * @param field     The sponsor field to set (defaults to sfSponsor)
 */
void
addSponsorToLedgerEntry(
    SLE::ref sle,
    SLE::const_ref sponsorSle,
    SF_ACCOUNT const& field = sfSponsor);

/**
 * Stamp the transaction's reserve sponsor onto a newly-created ledger entry.
 *
 * Equivalent to the overload above, but resolves the sponsor via
 * getTxReserveSponsor(ctx) instead of taking it explicitly. A no-op when the
 * transaction is not reserve-sponsored. The entry is assumed to be owned by
 * the transaction submitter, which is the only account a tx reserve sponsor
 * can cover.
 */
void
addSponsorToLedgerEntry(ApplyViewContext ctx, SLE::ref sle, SF_ACCOUNT const& field = sfSponsor);

/**
 * Remove the reserve sponsor field from a ledger entry.
 *
 * A no-op when @p field is not present on @p sle. For RippleState entries
 * the field must be sfHighSponsor or sfLowSponsor; for all other entry types
 * it must be sfSponsor.
 *
 * @param sle   The ledger entry to modify
 * @param field The sponsor field to clear (defaults to sfSponsor)
 */
void
removeSponsorFromLedgerEntry(SLE::ref sle, SF_ACCOUNT const& field = sfSponsor);

/**
 * Whether @p account is the owner of a ledger entry for sponsorship purposes.
 *
 * Ownership rules vary by entry type. For RippleState entries the owner is
 * whichever side of the trust line holds the reserve. For credentials, the
 * owner is the subject once accepted and the issuer before acceptance.
 *
 * @param view    The ledger read view (used for SignerList lookup)
 * @param sle     The ledger entry whose owner is checked
 * @param account The candidate account to match against
 * @return true if @p account owns @p sle, false otherwise.
 */
bool
isLedgerEntryOwner(ReadView const& view, SLE const& sle, AccountID const& account);

/**
 * Whether this ledger entry type can have a reserve sponsor attached to it.
 */
bool
isLedgerEntrySupportedBySponsorship(SLE const& sle);

/**
 * Return the number of owner-count units the ledger entry consumes.
 *
 * Most entries cost 1. Exceptions: Oracles scale with their price-data series
 * size, Vaults cost 2 (vault + pseudo-account), and legacy SignerList entries
 * (pre-MultiSignReserve) cost 2 + signer count.
 */
std::uint32_t
getLedgerEntryOwnerCount(SLE const& sle);

/**
 * Return the SField used to store the reserve sponsor for @p owner on @p sle.
 *
 * For most entry types this is sfSponsor. RippleState entries use
 * sfHighSponsor or sfLowSponsor depending on which side of the trust line
 * @p owner holds.
 *
 * @param sle   The ledger entry
 * @param owner The account whose sponsor field is needed
 * @return sfHighSponsor, sfLowSponsor, or sfSponsor as appropriate.
 */
SF_ACCOUNT const&
getLedgerEntrySponsorField(SLE const& sle, AccountID const& owner);

}  // namespace xrpl
