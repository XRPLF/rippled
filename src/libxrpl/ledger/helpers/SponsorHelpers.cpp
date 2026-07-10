#include <xrpl/ledger/helpers/SponsorHelpers.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/OracleHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <unordered_set>

namespace xrpl {

bool
isReserveSponsorAllowed(TxType txType)
{
    // Transaction types explicitly allow-listed for reserve sponsorship, for
    // v1. Lazily-initialized function-local static: constructed once on first
    // use, with no startup cost paid by clients that never call this.
    static std::unordered_set<TxType> const kReserveSponsorAllowed = {
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
    return kReserveSponsorAllowed.contains(txType);
}

std::optional<AccountID>
getTxReserveSponsorID(STTx const& tx)
{
    if (tx.isFieldPresent(sfSponsor) && isReserveSponsored(tx))
    {
        XRPL_ASSERT(
            getCurrentTransactionRules()->enabled(  // NOLINT(bugprone-unchecked-optional-access)
                featureSponsor),
            "xrpl::getTxReserveSponsorID : sponsor exists + Sponsor enabled");
        return tx.getAccountID(sfSponsor);
    }
    return {};
}

std::expected<SLE::pointer, TER>
getTxReserveSponsor(ApplyViewContext ctx)
{
    auto const sponsorID = getTxReserveSponsorID(ctx.tx);
    if (sponsorID)
    {
        XRPL_ASSERT(
            ctx.view.rules().enabled(featureSponsor),
            "xrpl::getTxReserveSponsor : sponsor exists + Sponsor enabled");
        auto sle = ctx.view.peek(keylet::account(*sponsorID));

        // already checked in Transactor::checkSponsor
        if (!sle)
            return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
        return sle;
    }
    return SLE::pointer();
}

std::expected<SLE::const_pointer, TER>
getTxReserveSponsor(ReadView const& view, STTx const& tx)
{
    auto const sponsorID = getTxReserveSponsorID(tx);
    if (sponsorID)
    {
        XRPL_ASSERT(
            view.rules().enabled(featureSponsor),
            "xrpl::getTxReserveSponsor : sponsor exists + Sponsor enabled");
        auto sle = view.read(keylet::account(*sponsorID));

        // already checked in Transactor::checkSponsor
        if (!sle)
            return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
        return sle;
    }
    return SLE::pointer();
}

std::expected<SLE::pointer, TER>
getEffectiveTxReserveSponsor(ApplyViewContext ctx, SLE::const_ref accountSle)
{
    // A reserve sponsor only covers tx.Account's own objects.
    if (ctx.view.rules().enabled(fixCleanup3_2_0))
    {
        XRPL_ASSERT(
            accountSle && accountSle->getType() == ltACCOUNT_ROOT,
            "xrpl::getEffectiveTxReserveSponsor : accountSle exists and is account type");
    }
    else
    {
        XRPL_ASSERT(
            accountSle &&
                ((accountSle->getType() == ltACCOUNT_ROOT) || (accountSle->getType() == ltESCROW)),
            "xrpl::getEffectiveTxReserveSponsor : accountSle exists and is account type");
    }

    if (isPseudoAccount(accountSle) || accountSle->getAccountID(sfAccount) != ctx.tx[sfAccount])
        return SLE::pointer();
    return getTxReserveSponsor(ctx);
}

std::optional<AccountID>
getLedgerEntryReserveSponsorID(SLE::const_ref sle, SF_ACCOUNT const& field)
{
    XRPL_ASSERT(
        (sle &&
         ((sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
          (sle->getType() != ltRIPPLE_STATE && field == sfSponsor))),
        "xrpl::getLedgerEntryReserveSponsorID : correct sfield");

    if (sle->isFieldPresent(field))
        return sle->getAccountID(field);
    return {};
}

SLE::pointer
getLedgerEntryReserveSponsor(ApplyView& view, SLE::const_ref sle, SF_ACCOUNT const& field)
{
    auto const sponsorID = getLedgerEntryReserveSponsorID(sle, field);
    if (sponsorID)
    {
        XRPL_ASSERT(
            view.rules().enabled(featureSponsor),
            "xrpl::getLedgerEntryReserveSponsor : sponsor exists + Sponsor enabled");
        return view.peek(keylet::account(*sponsorID));
    }
    return {};
}

void
addSponsorToLedgerEntry(SLE::ref sle, SLE::const_ref sponsorSle, SF_ACCOUNT const& field)
{
    XRPL_ASSERT(
        (sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
            (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
        "addSponsorToLedgerEntry : Invalid field to the LedgerEntry");
    if (sponsorSle)
    {
        XRPL_ASSERT(
            getCurrentTransactionRules()->enabled(  // NOLINT(bugprone-unchecked-optional-access)
                featureSponsor),
            "xrpl::addSponsorToLedgerEntry : sponsor exists + Sponsor enabled");
        sle->setAccountID(field, sponsorSle->getAccountID(sfAccount));
    }
}

void
addSponsorToLedgerEntry(ApplyViewContext ctx, SLE::ref sle, SF_ACCOUNT const& field)
{
    // getTxReserveSponsor yields a null pointer when the tx is not
    // reserve-sponsored, so addSponsorToLedgerEntry becomes a no-op then. The
    // error case (tecINTERNAL) is an already-checked invariant; skip stamping.
    auto const sponsorSle = getTxReserveSponsor(ctx);
    if (sponsorSle && *sponsorSle)
    {
        XRPL_ASSERT(
            ctx.view.rules().enabled(featureSponsor),
            "xrpl::addSponsorToLedgerEntry : sponsor exists + Sponsor enabled");
        addSponsorToLedgerEntry(sle, *sponsorSle, field);
    }
}

void
removeSponsorFromLedgerEntry(SLE::ref sle, SF_ACCOUNT const& field)
{
    XRPL_ASSERT(
        (sle->getType() == ltRIPPLE_STATE && (field == sfHighSponsor || field == sfLowSponsor)) ||
            (sle->getType() != ltRIPPLE_STATE && field == sfSponsor),
        "removeSponsorFromLedgerEntry : Invalid field to the LedgerEntry");
    if (sle->isFieldPresent(field))
    {
        XRPL_ASSERT(
            getCurrentTransactionRules()->enabled(  // NOLINT(bugprone-unchecked-optional-access)
                featureSponsor),
            "xrpl::removeSponsorFromLedgerEntry : sponsor exists + Sponsor enabled");
        sle->makeFieldAbsent(field);
    }
}

bool
isLedgerEntryOwner(ReadView const& view, SLE const& sle, AccountID const& account)
{
    switch (sle.getType())
    {
        case ltCHECK:
        case ltESCROW:
        case ltPAYCHAN:
        case ltMPTOKEN:
        case ltDELEGATE:
        case ltDEPOSIT_PREAUTH:
            return sle.getAccountID(sfAccount) == account;
        case ltMPTOKEN_ISSUANCE:
            return sle.getAccountID(sfIssuer) == account;
        case ltSIGNER_LIST: {
            auto const signerList = view.read(keylet::signerList(account));
            if (!signerList)
                return false;
            return signerList->key() == sle.key();
        }
        case ltCREDENTIAL: {
            auto const& ownerField = sle.isFlag(lsfAccepted) ? sfSubject : sfIssuer;
            return sle.getAccountID(ownerField) == account;
        }
        case ltRIPPLE_STATE: {
            if (sle.isFlag(lsfHighReserve))
            {
                auto const highAccount = sle.getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == account)
                    return true;
            }
            if (sle.isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle.getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == account)
                    return true;
            }
            // Reachable: the sponsee may be a third party or the side of the
            // line that holds no reserve (e.g. the issuer). Callers map this
            // to tecNO_PERMISSION.
            return false;
        }
        default:
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::isLedgerEntryOwner : object is not supported by sponsorship.");
            return false;
            // LCOV_EXCL_STOP
    };
}

bool
isLedgerEntrySupportedBySponsorship(SLE const& sle)
{
    switch (sle.getType())
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

std::uint32_t
getLedgerEntryOwnerCount(SLE const& sle)
{
    switch (sle.getType())
    {
        case ltORACLE: {
            return calculateOracleReserve(sle.getFieldArray(sfPriceDataSeries));
        }
        // Vaults require 2 owner counts (the vault and a pseudo-account)
        case ltVAULT:
            return 2;
        case ltSIGNER_LIST: {
            // Mirror SignerListSet's owner-count accounting so that create and
            // delete agree. Modern lists (post-MultiSignReserve) carry the
            // lsfOneOwnerCount flag and cost a single owner count. Legacy
            // pre-MultiSignReserve lists cost 2 + signer_count owner counts
            if (sle.isFlag(lsfOneOwnerCount))
                return 1;
            return 2 + static_cast<std::uint32_t>(sle.getFieldArray(sfSignerEntries).size());
        }
        case ltACCOUNT_ROOT:
            // LCOV_EXCL_START
            UNREACHABLE("AccountRoots are not supported by object sponsorship.");
            return 0;
            // LCOV_EXCL_STOP
        default:
            return 1;
    }
}

SF_ACCOUNT const&
getLedgerEntrySponsorField(SLE const& sle, AccountID const& owner)
{
    switch (sle.getType())
    {
        case ltRIPPLE_STATE: {
            if (sle.isFlag(lsfHighReserve))
            {
                auto const highAccount = sle.getFieldAmount(sfHighLimit).getIssuer();
                if (highAccount == owner)
                    return sfHighSponsor;
            }
            if (sle.isFlag(lsfLowReserve))
            {
                auto const lowAccount = sle.getFieldAmount(sfLowLimit).getIssuer();
                if (lowAccount == owner)
                    return sfLowSponsor;
            }
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::getLedgerEntrySponsorField : unknown owner for RippleState");
            return sfSponsor;
            // LCOV_EXCL_STOP
        }
        default:
            return sfSponsor;
    }
}

}  // namespace xrpl
