#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/OracleHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
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
    return (tx.getFieldU32(sfSponsorFlags) & spfSponsorFee) != 0u;
}

inline bool
isReserveSponsored(STTx const& tx)
{
    return (tx.getFieldU32(sfSponsorFlags) & spfSponsorReserve) != 0u;
}

std::optional<AccountID>
getTxReserveSponsorAccountID(STTx const& tx);

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
getLedgerEntryReserveSponsorAccountID(SLE::const_ref sle, SF_ACCOUNT const& field = sfSponsor);

SLE::pointer
getLedgerEntryReserveSponsor(
    ApplyView& view,
    SLE::const_ref sle,
    SF_ACCOUNT const& field = sfSponsor);

SLE::const_pointer
getLedgerEntryReserveSponsor(
    ReadView const& view,
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

template <typename T>
inline std::optional<AccountID>
getLedgerEntryOwner(ReadView const& view, T const& sle, AccountID const& account)
{
    switch (sle->getType())
    {
        case ltCHECK:
        case ltESCROW:
        case ltPAYCHAN:
        case ltMPTOKEN:
        case ltDELEGATE:
        case ltDEPOSIT_PREAUTH:
            return sle->getAccountID(sfAccount);
        case ltMPTOKEN_ISSUANCE:
            return sle->getAccountID(sfIssuer);
        case ltSIGNER_LIST: {
            auto const signerList = view.read(keylet::signerList(account));
            if (!signerList)
                return std::nullopt;
            if (signerList->key() == sle->key())
                return account;
            return std::nullopt;
        }
        case ltCREDENTIAL: {
            if (sle->isFlag(lsfAccepted))
                return sle->getAccountID(sfSubject);
            return sle->getAccountID(sfIssuer);
        }
        case ltRIPPLE_STATE: {
            if (sle->isFlag(lsfHighReserve))
            {
                auto const highAccount = sle->getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == account)
                    return highAccount;
            }
            if (sle->isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle->getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == account)
                    return lowAccount;
            }
            return std::nullopt;
        }
        default:
            UNREACHABLE("Object is not supported by sponsorship.");
            return std::nullopt;
    };
}

template <typename T>
inline bool
isLedgerEntrySupportedBySponsorship(T const& sle)
{
    switch (sle->getType())
    {
        case ltCHECK:
        case ltESCROW:
        case ltPAYCHAN:
        case ltMPTOKEN:
        case ltDELEGATE:
        case ltDEPOSIT_PREAUTH:
        case ltMPTOKEN_ISSUANCE:
        case ltSIGNER_LIST:
        case ltCREDENTIAL:
        case ltRIPPLE_STATE:
            return true;
        default:
            return false;
    };
}

template <typename T>
inline std::uint32_t
getLedgerEntryOwnerCount(T const& sle)
{
    switch (sle->getType())
    {
        case ltORACLE: {
            return calculateOracleReserve(sle->getFieldArray(sfPriceDataSeries).size());
        }
        // Vaults require 2 owner counts (the vault and a pseudo-account)
        case ltVAULT:
            return 2;
        case ltSIGNER_LIST: {
            // Mirror SignerListSet's owner-count accounting so that create and
            // delete agree. Modern lists (post-MultiSignReserve) carry the
            // lsfOneOwnerCount flag and cost a single owner count. Legacy
            // pre-MultiSignReserve lists cost 2 + signer_count owner counts
            if (sle->isFlag(lsfOneOwnerCount))
                return 1;
            return 2 + static_cast<std::uint32_t>(sle->getFieldArray(sfSignerEntries).size());
        }
        case ltACCOUNT_ROOT:
            UNREACHABLE("AccountRoots are not supported by object sponsorship.");
            return 0;
        default:
            return 1;
    }
};

template <typename T>
inline SF_ACCOUNT const&
getLedgerEntrySponsorField(T const& sle, AccountID const& owner)
{
    switch (sle->getType())
    {
        case ltRIPPLE_STATE: {
            if (sle->isFlag(lsfHighReserve))
            {
                auto const highAccount = sle->getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == owner)
                    return sfHighSponsor;
            }
            if (sle->isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle->getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == owner)
                    return sfLowSponsor;
            }
            // LCOV_EXCL_START
            UNREACHABLE("Should not happen. Owner should be checked before calling this function.");
            return sfSponsor;
            // LCOV_EXCL_STOP
        }
        default:
            return sfSponsor;
    }
};

}  // namespace xrpl
