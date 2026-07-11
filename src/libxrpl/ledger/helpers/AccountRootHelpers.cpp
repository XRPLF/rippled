#include <xrpl/ledger/helpers/AccountRootHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OwnerCounts.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace xrpl {

bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer)
{
    if (isXRP(issuer))
        return false;
    if (auto const sle = view.read(keylet::account(issuer)))
        return sle->isFlag(lsfGlobalFreeze);
    return false;
}

namespace {

// An owner count cannot be negative. If adjustment would cause a negative
// owner count, clamp the owner count at 0. Similarly for overflow. This
// adjustment allows the ownerCount to be adjusted up or down in multiple steps.
// If id != std::nullopt, then do error reporting.
//
// Returns adjusted owner count.
std::uint32_t
confineOwnerCount(
    std::uint32_t currentOwnerCount,
    std::int32_t ownerCountAdj,
    std::optional<AccountID> const& id = std::nullopt,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
{
    std::uint32_t totalOwnerCount{currentOwnerCount + ownerCountAdj};
    if (ownerCountAdj > 0)
    {
        // Overflow is well defined on unsigned
        if (totalOwnerCount < currentOwnerCount)
        {
            // LCOV_EXCL_START
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count exceeds max!";
            }
            totalOwnerCount = std::numeric_limits<std::uint32_t>::max();
            // LCOV_EXCL_STOP
        }
    }
    else
    {
        // Underflow is well defined on unsigned
        if (totalOwnerCount > currentOwnerCount)
        {
            // LCOV_EXCL_START
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count set below 0!";
            }
            totalOwnerCount = 0;
            XRPL_ASSERT(!id, "xrpl::confineOwnerCount : id is not set");
            // LCOV_EXCL_STOP
        }
    }
    return totalOwnerCount;
}

// Returns the number of account reserves funded by this account: 1 for itself (0 if sponsored by
// another account) plus the count of accounts it sponsors.
std::uint32_t
accountCountImpl(SLE::const_ref sle, std::int32_t accountCountAdj, beast::Journal j)
{
    bool const isSponsored = sle->isFieldPresent(sfSponsor);
    std::int64_t const sponsoringAccountCount = sle->getFieldU32(sfSponsoringAccountCount);
    std::int64_t const currentAccountCount = (isSponsored ? 0 : 1) + sponsoringAccountCount;

    std::int64_t totalAccountCount{currentAccountCount + accountCountAdj};
    if (totalAccountCount > std::numeric_limits<std::uint32_t>::max())
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Reserve count exceeds max!";
        totalAccountCount = std::numeric_limits<std::uint32_t>::max();
        // LCOV_EXCL_STOP
    }
    else if (totalAccountCount < 0)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::accountCountImpl : Reserve count set below 0");
        JLOG(j.fatal()) << "Reserve count set below 0";
        totalAccountCount = 0;
        // LCOV_EXCL_STOP
    }

    return totalAccountCount;
}

std::uint32_t
adjustOwnerCountImpl(
    ApplyView& view,
    SLE::ref sle,
    SF_UINT32 const& sfield,
    AccountID const& accID,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    std::uint32_t const currentOwnerCount = sle->at(sfield);
    std::uint32_t const totalOwnerCount =
        confineOwnerCount(currentOwnerCount, ownerCountAdj, accID, j);
    sle->at(sfield) = totalOwnerCount;
    view.update(sle);
    return totalOwnerCount;
}

void
adjustOwnerCountSigned(
    ApplyView& view,
    SLE::ref accountSle,
    SLE::ref sponsorSle,
    std::int32_t adjustment,
    beast::Journal j)
{
    if (view.rules().enabled(featureSponsor))
    {
        XRPL_ASSERT(accountSle, "xrpl::adjustOwnerCountSigned : valid account sle");
        if (!accountSle)
            return;  // LCOV_EXCL_LINE

        auto const accountID = accountSle->getAccountID(sfAccount);
        bool const validType = accountSle->getType() == ltACCOUNT_ROOT;
        XRPL_ASSERT(validType, "xrpl::adjustOwnerCountSigned : valid account sle type");
        if (!validType)
            return;  // LCOV_EXCL_LINE

        XRPL_ASSERT(adjustment, "xrpl::adjustOwnerCountSigned : nonzero adjustment input");

        OwnerCounts const currentOwnerCount(accountSle);
        OwnerCounts totalOwnerCount(currentOwnerCount);

        if (sponsorSle)
        {
            bool const validSponsorType = sponsorSle->getType() == ltACCOUNT_ROOT;
            XRPL_ASSERT(validSponsorType, "xrpl::adjustOwnerCountSigned : valid sponsor sle type");
            if (!validSponsorType)
                return;  // LCOV_EXCL_LINE
            auto const sponsorID = sponsorSle->getAccountID(sfAccount);

            totalOwnerCount.sponsored = adjustOwnerCountImpl(
                view, accountSle, sfSponsoredOwnerCount, accountID, adjustment, j);

            {
                OwnerCounts const sponsorCurrent(sponsorSle);
                OwnerCounts sponsorAdjustment(sponsorCurrent);
                sponsorAdjustment.sponsoring = adjustOwnerCountImpl(
                    view, sponsorSle, sfSponsoringOwnerCount, sponsorID, adjustment, j);
                view.adjustOwnerCountHook(sponsorID, sponsorCurrent, sponsorAdjustment);
            }

            auto sponsorshipSle = view.peek(keylet::sponsorship(sponsorID, accountID));
            if (sponsorshipSle && adjustment > 0)
            {
                // Only decrease the pre-funded ReserveCount on Sponsorship if we assign new
                // objects. Removing/reassigning ownership of the object doesn't increase
                // RemainingOwnerCount back. Don't call hook because this counter is not something
                // that requires reserve (like other sf...OwnerCounts do).
                adjustOwnerCountImpl(
                    view, sponsorshipSle, sfRemainingOwnerCount, sponsorID, -adjustment, j);
            }
        }

        totalOwnerCount.owner =
            adjustOwnerCountImpl(view, accountSle, sfOwnerCount, accountID, adjustment, j);
        view.adjustOwnerCountHook(accountID, currentOwnerCount, totalOwnerCount);
    }
    else
    {
        XRPL_ASSERT(accountSle, "xrpl::adjustOwnerCountSigned : valid account sle");
        if (!accountSle)
            return;
        // the remaining are only asserts to preserve existing behavior
        XRPL_ASSERT(sponsorSle == nullptr, "xrpl::adjustOwnerCountSigned : sponsor not enabled");
        XRPL_ASSERT(
            accountSle->getType() == ltACCOUNT_ROOT,
            "xrpl::adjustOwnerCountSigned : valid account sle type");
        XRPL_ASSERT(adjustment, "xrpl::adjustOwnerCount : nonzero adjustment input");
        std::uint32_t const current{accountSle->getFieldU32(sfOwnerCount)};
        AccountID const id = (*accountSle)[sfAccount];
        std::uint32_t const adjusted = confineOwnerCount(current, adjustment, id, j);

        OwnerCounts const currentOwnerCount(accountSle);
        OwnerCounts finalOwnerCount(currentOwnerCount);
        finalOwnerCount.owner = adjusted;

        view.adjustOwnerCountHook(id, currentOwnerCount, finalOwnerCount);
        accountSle->at(sfOwnerCount) = adjusted;
        view.update(accountSle);
    }
}

}  // namespace

std::uint32_t
ownerCount(SLE::const_ref sle, beast::Journal j, std::int32_t ownerCountAdj)
{
    XRPL_ASSERT(sle && sle->getType() == ltACCOUNT_ROOT, "xrpl::ownerCount : sle is account root");

    AccountID const id = sle->getAccountID(sfAccount);
    std::uint32_t const currentOwnerCount = sle->at(sfOwnerCount);
    std::uint32_t const sponsoredOwnerCount = sle->at(sfSponsoredOwnerCount);
    std::uint32_t const sponsoringOwnerCount = sle->at(sfSponsoringOwnerCount);

    XRPL_ASSERT(
        currentOwnerCount >= sponsoredOwnerCount,
        "xrpl::ownerCount : OwnerCount must be greater than or equal to SponsoredOwnerCount");

    std::int64_t deltaCount =
        static_cast<std::int64_t>(ownerCountAdj) - sponsoredOwnerCount + sponsoringOwnerCount;

    if (deltaCount > std::numeric_limits<std::int32_t>::max())
    {
        // LCOV_EXCL_START
        deltaCount = std::numeric_limits<std::int32_t>::max();
        JLOG(j.fatal()) << "Account " << id << " delta count exceeds max, "
                        << "adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << sponsoredOwnerCount
                        << ", sponsoringOwnerCount: " << sponsoringOwnerCount;
        // LCOV_EXCL_STOP
    }
    else if (deltaCount < std::numeric_limits<std::int32_t>::min())
    {
        // LCOV_EXCL_START
        deltaCount = std::numeric_limits<std::int32_t>::min();
        JLOG(j.fatal()) << "Account " << id << " delta count is below min, "
                        << "adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << sponsoredOwnerCount
                        << ", sponsoringCount: " << sponsoringOwnerCount;
        // LCOV_EXCL_STOP
    }

    return confineOwnerCount(currentOwnerCount, deltaCount);
}

XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j)
{
    auto const sle = view.read(keylet::account(id));
    if (sle == nullptr)
        return beast::kZero;

    // Return balance minus reserve
    std::uint32_t const currentOwnerCount =
        confineOwnerCount(view.ownerCountHook(id, OwnerCounts(sle)).count(), ownerCountAdj);
    std::uint32_t const currentAccountCount = accountCountImpl(sle, 0, j);

    // Pseudo-accounts have no reserve requirement
    auto const reserve = isPseudoAccount(sle)
        ? XRPAmount{0}
        : view.fees().accountReserve(currentOwnerCount, currentAccountCount);

    auto const fullBalance = sle->getFieldAmount(sfBalance);

    auto const balance = view.balanceHookIOU(id, xrpAccount(), fullBalance);

    STAmount const amount = (balance < reserve) ? STAmount{0} : balance - reserve;

    JLOG(j.trace()) << "accountHolds:" << " account=" << to_string(id)
                    << " amount=" << amount.getFullText()
                    << " fullBalance=" << fullBalance.getFullText()
                    << " balance=" << balance.getFullText() << " reserve=" << reserve
                    << " ownerCount=" << currentOwnerCount << " ownerCountAdj=" << ownerCountAdj;

    return amount.xrp();
}

Rate
transferRate(ReadView const& view, AccountID const& issuer)
{
    auto const sle = view.read(keylet::account(issuer));

    if (sle && sle->isFieldPresent(sfTransferRate))
        return Rate{sle->getFieldU32(sfTransferRate)};

    return kParityRate;
}

void
increaseOwnerCount(
    ApplyView& view,
    SLE::ref accountSle,
    SLE::ref sponsorSle,
    std::uint32_t count,
    beast::Journal j)
{
    XRPL_ASSERT(
        count != 0 && count <= std::numeric_limits<std::int32_t>::max(),
        "xrpl::increaseOwnerCount : count in signed delta range");
    if (count == 0 || count > std::numeric_limits<std::int32_t>::max())
        return;  // LCOV_EXCL_LINE

    adjustOwnerCountSigned(view, accountSle, sponsorSle, static_cast<std::int32_t>(count), j);
}

void
increaseOwnerCount(ApplyViewContext ctx, SLE::ref accountSle, std::uint32_t count, beast::Journal j)
{
    auto const sponsorExp = getEffectiveTxReserveSponsor(ctx, accountSle);

    // The sponsor's existence is validated by checkReserve/checkSponsor before
    // any owner-count mutation, so loading it here cannot fail.
    XRPL_ASSERT(
        sponsorExp.has_value(), "xrpl::increaseOwnerCount : sponsor validated before mutation");

    increaseOwnerCount(ctx.view, accountSle, sponsorExp ? *sponsorExp : SLE::pointer(), count, j);
}

void
decreaseOwnerCount(
    ApplyView& view,
    SLE::ref accountSle,
    SLE::ref sponsorSle,
    std::uint32_t count,
    beast::Journal j)
{
    XRPL_ASSERT(
        count != 0 && count <= std::numeric_limits<std::int32_t>::max(),
        "xrpl::decreaseOwnerCount : count in signed delta range");
    if (count == 0 || count > std::numeric_limits<std::int32_t>::max())
        return;  // LCOV_EXCL_LINE

    adjustOwnerCountSigned(view, accountSle, sponsorSle, -static_cast<std::int32_t>(count), j);
}

void
decreaseOwnerCountForObject(
    ApplyView& view,
    SLE::ref accountSle,
    SLE::ref objectSle,
    std::uint32_t count,
    beast::Journal j)
{
    XRPL_ASSERT(objectSle, "xrpl::decreaseOwnerCountForObject : valid object sle");
    if (!objectSle)
        return;  // LCOV_EXCL_LINE

    bool const validObjectType = objectSle->getType() != ltACCOUNT_ROOT;
    XRPL_ASSERT(validObjectType, "xrpl::decreaseOwnerCountForObject : valid object sle type");
    if (!validObjectType)
        return;  // LCOV_EXCL_LINE

    SLE::ref sponsorSle = getLedgerEntryReserveSponsor(view, objectSle);
    decreaseOwnerCount(view, accountSle, sponsorSle, count, j);
}

void
adjustLoanBrokerOwnerCount(
    ApplyView& view,
    SLE::ref brokerSle,
    std::int32_t delta,
    beast::Journal j)
{
    XRPL_ASSERT(
        brokerSle && brokerSle->getType() == ltLOAN_BROKER,
        "xrpl::adjustLoanBrokerOwnerCount : valid loan broker sle");
    if (!brokerSle || brokerSle->getType() != ltLOAN_BROKER)
        return;  // LCOV_EXCL_LINE

    XRPL_ASSERT(delta != 0, "xrpl::adjustLoanBrokerOwnerCount : nonzero delta input");
    if (delta == 0)
        return;  // LCOV_EXCL_LINE

    adjustOwnerCountImpl(
        view, brokerSle, sfOwnerCount, brokerSle->getAccountID(sfAccount), delta, j);
}

XRPAmount
accountReserve(ReadView const& view, SLE::const_ref sle, beast::Journal j, Adjustment adj)
{
    XRPL_ASSERT(sle && sle->getType() == ltACCOUNT_ROOT, "xrpl::accountReserve : valid sle");

    if (!view.rules().enabled(featureSponsor))
    {
        XRPL_ASSERT(adj.accountCountDelta == 0, "xrpl::accountReserve : no account count delta");
        return view.fees().accountReserve(sle->getFieldU32(sfOwnerCount) + adj.ownerCountDelta, 1);
    }
    std::uint32_t const currentOwnerCount = ownerCount(sle, j, adj.ownerCountDelta);
    std::uint32_t const currentAccountCount = accountCountImpl(sle, adj.accountCountDelta, j);

    return view.fees().accountReserve(currentOwnerCount, currentAccountCount);
}

TER
checkReserve(
    ApplyViewContext ctx,
    SLE::const_ref accSle,
    XRPAmount accBalance,
    SLE::const_ref sponsorSle,
    Adjustment adj,
    beast::Journal j,
    TER insufReserveCode)
{
    // TODO: swap to assert after fixCleanup3_2_0 is retired
    if (!accSle || accSle->getType() != ltACCOUNT_ROOT)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    XRPL_ASSERT(
        !isTesSuccess(insufReserveCode), "xrpl::checkReserve : insufReserveCode is not tesSUCCESS");
    if (ctx.view.rules().enabled(featureSponsor))
    {
        if (sponsorSle)
        {
            if (sponsorSle->getType() != ltACCOUNT_ROOT)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            auto const sle = ctx.view.read(
                keylet::sponsorship(
                    sponsorSle->getAccountID(sfAccount), accSle->getAccountID(sfAccount)));

            // A reserve-sponsored tx must carry a sponsor signature
            // (cosigning path) and/or have a pre-existing sponsorship SLE
            // (prefunded path). Absence of both is an internal invariant break.
            if (isReserveSponsored(ctx.tx) && !sle && !ctx.tx.isFieldPresent(sfSponsorSignature))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (sle)
            {
                auto const ownerCountAllowed = sle->getFieldU32(sfRemainingOwnerCount);
                if (adj.ownerCountDelta > 0 &&
                    ownerCountAllowed < static_cast<std::uint32_t>(adj.ownerCountDelta))
                    return insufReserveCode;
            }

            auto const sponsorBalance = sponsorSle->getFieldAmount(sfBalance).xrp();
            XRPAmount const sponsorReserve = accountReserve(ctx.view, sponsorSle, j, adj);

            if (sponsorBalance < sponsorReserve)
                return insufReserveCode;
        }
        else
        {
            XRPAmount const reserve = accountReserve(ctx.view, accSle, j, adj);
            if (accBalance < reserve)
                return insufReserveCode;
        }
    }
    else
    {
        XRPL_ASSERT(
            !sponsorSle,
            "xrpl::checkReserve : featureSponsor disabled and sponsorSle not provided");
        XRPL_ASSERT(adj.accountCountDelta == 0, "xrpl::checkReserve : accountCountDelta is 0");
        auto const reserve = ctx.view.fees().accountReserve(
            accSle->getFieldU32(sfOwnerCount) + adj.ownerCountDelta, 1);
        if (accBalance < reserve)
            return insufReserveCode;
    }
    return tesSUCCESS;
}

TER
checkReserve(
    ApplyViewContext ctx,
    SLE::const_ref accSle,
    XRPAmount accBalance,
    Adjustment adj,
    beast::Journal j)
{
    auto const sponsorExp = getEffectiveTxReserveSponsor(ctx, accSle);
    if (!sponsorExp)
        return sponsorExp.error();  // LCOV_EXCL_LINE
    return checkReserve(ctx, accSle, accBalance, *sponsorExp, adj, j);
}

// ----------------------------------------------------

AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey)
{
    // This number must not be changed without an amendment
    static constexpr std::uint16_t kMaxAccountAttempts = 256;
    for (std::uint16_t i = 0; i < kMaxAccountAttempts; ++i)
    {
        RipeshaHasher rsh;
        auto const hash = sha512Half(i, view.header().parentHash, pseudoOwnerKey);
        rsh(hash.data(), hash.size());
        AccountID const ret = AccountID::fromRaw(static_cast<RipeshaHasher::result_type>(rsh));
        if (!view.read(keylet::account(ret)))
            return ret;
    }
    return beast::kZero;
}

// Pseudo-account designator fields MUST be maintained by including the
// SField::sMD_PseudoAccount flag in the SField definition. (Don't forget to
// "| SField::sMD_Default"!) The fields do NOT need to be amendment-gated,
// since a non-active amendment will not set any field, by definition.
// Specific properties of a pseudo-account are NOT checked here, that's what
// InvariantCheck is for.
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields()
{
    static std::vector<SField const*> const kPseudoFields = []() {
        auto const ar = LedgerFormats::getInstance().findByType(ltACCOUNT_ROOT);
        if (!ar)
        {
            // LCOV_EXCL_START
            Throw<std::logic_error>(
                "xrpl::getPseudoAccountFields : unable to find account root "
                "ledger format");
            // LCOV_EXCL_STOP
        }
        auto const& soTemplate = ar->getSOTemplate();

        std::vector<SField const*> pseudoFields;
        for (auto const& field : soTemplate)
        {
            if (field.sField().shouldMeta(SField::kSmdPseudoAccount))
                pseudoFields.emplace_back(&field.sField());
        }
        return pseudoFields;
    }();
    return kPseudoFields;
}

[[nodiscard]] bool
isPseudoAccount(SLE::const_pointer sleAcct, std::set<SField const*> const& pseudoFieldFilter)
{
    auto const& fields = getPseudoAccountFields();

    // Intentionally use defensive coding here because it's cheap and makes the
    // semantics of true return value clean.
    return sleAcct && sleAcct->getType() == ltACCOUNT_ROOT &&
        std::count_if(
            fields.begin(), fields.end(), [&sleAcct, &pseudoFieldFilter](SField const* sf) -> bool {
                return sleAcct->isFieldPresent(*sf) &&
                    (pseudoFieldFilter.empty() || pseudoFieldFilter.contains(sf));
            }) > 0;
}

std::expected<SLE::pointer, TER>
createPseudoAccount(ApplyView& view, uint256 const& pseudoOwnerKey, SField const& ownerField)
{
    [[maybe_unused]]
    auto const& fields = getPseudoAccountFields();
    XRPL_ASSERT(
        std::count_if(
            fields.begin(),
            fields.end(),
            [&ownerField](SField const* sf) -> bool { return *sf == ownerField; }) == 1,
        "xrpl::createPseudoAccount : valid owner field");

    auto const accountId = pseudoAccountAddress(view, pseudoOwnerKey);
    if (accountId == beast::kZero)
        return std::unexpected(tecDUPLICATE);

    // Create pseudo-account.
    auto account = std::make_shared<SLE>(keylet::account(accountId));
    account->setAccountID(sfAccount, accountId);
    account->setFieldAmount(sfBalance, STAmount{});

    // Pseudo-accounts can't submit transactions, so set the sequence number
    // to 0 to make them easier to spot and verify, and add an extra level
    // of protection.
    std::uint32_t const seqno =                           //
        view.rules().enabled(featureSingleAssetVault) ||  //
            view.rules().enabled(featureLendingProtocol)  //
        ? 0                                               //
        : view.seq();
    account->setFieldU32(sfSequence, seqno);
    // Ignore reserves requirement, disable the master key, allow default
    // rippling, and enable deposit authorization to prevent payments into
    // pseudo-account.
    account->setFieldU32(sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
    // Link the pseudo-account with its owner object.
    account->setFieldH256(ownerField, pseudoOwnerKey);

    view.insert(account);

    return account;
}

[[nodiscard]] TER
checkDestinationAndTag(SLE::const_ref toSle, bool hasDestinationTag)
{
    if (toSle == nullptr)
        return tecNO_DST;

    // The tag is basically account-specific information we don't
    // understand, but we can require someone to fill it in.
    if (toSle->isFlag(lsfRequireDestTag) && !hasDestinationTag)
        return tecDST_TAG_NEEDED;  // Cannot send without a tag

    return tesSUCCESS;
}

}  // namespace xrpl
