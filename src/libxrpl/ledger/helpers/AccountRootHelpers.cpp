#include <xrpl/ledger/helpers/AccountRootHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
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

// An owner count cannot be negative. If adjustment would cause a negative
// owner count, clamp the owner count at 0. Similarly for overflow. This
// adjustment allows the ownerCount to be adjusted up or down in multiple steps.
// If id != std::nullopt, then do error reporting.
//
// Returns adjusted owner count.
static std::uint32_t
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
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count exceeds max!";
            }
            totalOwnerCount = std::numeric_limits<std::uint32_t>::max();
        }
    }
    else
    {
        // Underflow is well defined on unsigned
        if (totalOwnerCount > currentOwnerCount)
        {
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count set below 0!";
            }
            totalOwnerCount = 0;
            XRPL_ASSERT(!id, "xrpl::confineOwnerCount : id is not set");
        }
    }
    return totalOwnerCount;
}

// Return number of the accounts which reserve is covered by current account (so called "reserve
// count")
static std::uint32_t
accountCountImpl(SLE::const_ref sle, std::int32_t accountCountAdj, beast::Journal j)
{
    bool const isSponsored = sle->isFieldPresent(sfSponsor);
    std::int64_t const sponsoringAccountCount = sle->getFieldU32(sfSponsoringAccountCount);
    std::int64_t const currentAccountCount = (isSponsored ? 0 : 1) + sponsoringAccountCount;

    std::int64_t totalAccountCount{currentAccountCount + accountCountAdj};
    if (totalAccountCount > std::numeric_limits<std::uint32_t>::max())
    {
        JLOG(j.error()) << "Reserve count exceeds max!";
        totalAccountCount = std::numeric_limits<std::uint32_t>::max();
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
ownerCount(SLE::const_ref sle, beast::Journal j, std::int32_t ownerCountAdj)
{
    XRPL_ASSERT(sle && sle->getType() == ltACCOUNT_ROOT, "xrpl::ownerCount : sle is account root");

    AccountID const id = sle->getAccountID(sfAccount);
    std::uint32_t const currentOwnerCount = sle->at(sfOwnerCount);
    std::uint32_t const sponsoredOwnerCount = sle->at(sfSponsoredOwnerCount);
    std::uint32_t const sponsoringOwnerCount = sle->at(sfSponsoringOwnerCount);

    XRPL_ASSERT(
        currentOwnerCount >= sponsoredOwnerCount,
        "xrpl::ownerCount : OwnerCount must be greater than or equal to "
        "SponsoredOwnerCount");

    std::int64_t deltaCount =
        static_cast<std::int64_t>(ownerCountAdj) - sponsoredOwnerCount + sponsoringOwnerCount;

    if (deltaCount > std::numeric_limits<std::int32_t>::max())
    {
        deltaCount = std::numeric_limits<std::int32_t>::max();
        JLOG(j.fatal()) << "Account " << id << " delta count exceeds max, "
                        << "adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << sponsoredOwnerCount
                        << ", sponsoringOwnerCount: " << sponsoringOwnerCount;
    }
    else if (deltaCount < std::numeric_limits<std::int32_t>::min())
    {
        deltaCount = std::numeric_limits<std::int32_t>::min();
        JLOG(j.fatal()) << "Account " << id << " delta count is below min, "
                        << "adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << sponsoredOwnerCount
                        << ", sponsoringCount: " << sponsoringOwnerCount;
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
        confineOwnerCount(view.ownerCountHook(id, ownerCount(sle, j)), ownerCountAdj);
    std::uint32_t const currentAccountCount = accountCountImpl(sle, 0, j);

    // Pseudo-accounts have no reserve requirement
    auto const reserve = isPseudoAccount(sle)
        ? XRPAmount{0}
        : baseAccountReserve(view, currentOwnerCount, currentAccountCount);

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

static void
adjustOwnerCountValue(
    ApplyView& view,
    SLE::ref sle,
    SF_UINT32 const& sfield,
    AccountID const& accID,
    std::int32_t ownerCountAdj,
    beast::Journal j,
    bool callHook = true)
{
    std::uint32_t const currentOwnerCount = sle->at(sfield);
    std::uint32_t const totalOwnerCount =
        confineOwnerCount(currentOwnerCount, ownerCountAdj, accID, j);
    if (callHook)
        view.adjustOwnerCountHook(accID, currentOwnerCount, totalOwnerCount);
    sle->at(sfield) = totalOwnerCount;
    view.update(sle);
}

void
adjustOwnerCountImpl(
    ApplyView& view,
    ReserveContext& reserveCtx,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    auto& accountSle = reserveCtx.accountSle;
    if (!accountSle)
        Throw<std::runtime_error>("xrpl::adjustOwnerCount : valid account sle");

    auto const sleType = reserveCtx.accountSle->getType();
    bool const validType = reserveCtx.sponsorSle
        ? sleType == ltACCOUNT_ROOT
        : sleType == ltLOAN_BROKER || sleType == ltACCOUNT_ROOT;
    if (!validType)
        Throw<std::logic_error>("xrpl::adjustOwnerCount : valid account sle type");

    XRPL_ASSERT(ownerCountAdj, "xrpl::adjustOwnerCount : nonzero ownerCountAdj input");
    if (ownerCountAdj == 0)
        return;

    if (reserveCtx.isSponsored())
    {
        if (reserveCtx.sponsorSle->getType() != ltACCOUNT_ROOT)
            Throw<std::logic_error>("xrpl::adjustOwnerCount : valid sponsor sle type");

        adjustOwnerCountValue(
            ctx.view,
            reserveCtx.accountSle,
            sfSponsoredOwnerCount,
            reserveCtx.accountID,
            ownerCountAdj,
            j);
        adjustOwnerCountValue(
            ctx.view,
            reserveCtx.sponsorSle,
            sfSponsoringOwnerCount,
            *reserveCtx.sponsorID,
            ownerCountAdj,
            j);

        if (reserveCtx.sponsorshipSle && ownerCountAdj > 0)
        {
            // update the pre-funded RemainingOwnerCount on Sponsorship ledger object
            // Remaining owner count moves opposite to ownerCountAdj:
            // +ownerCountAdj => consume reserve (-),
            adjustOwnerCountValue(
                ctx.view,
                reserveCtx.sponsorshipSle,
                sfRemainingOwnerCount,
                *reserveCtx.sponsorID,
                -ownerCountAdj,
                j,
                false);
        }
    }
    adjustOwnerCountValue(
        ctx.view, reserveCtx.accountSle, sfOwnerCount, reserveCtx.accountID, ownerCountAdj, j);
}

void
adjustOwnerCount(ApplyViewContext& ctx, std::int32_t ownerCountAdj, beast::Journal j)
{
    return adjustOwnerCountImpl(ctx.view, ctx.reserveCtx, ownerCountAdj, j);
}

void
adjustOwnerCountObj(
    ApplyViewContext& ctx,
    SLE::ref objectSle,
    std::int32_t accountCountAdj,
    beast::Journal j)
{
    if (!objectSle)
        Throw<std::runtime_error>("xrpl::adjustOwnerCount : valid object sle");
    if (objectSle->getType() == ltACCOUNT_ROOT)
        Throw<std::logic_error>("xrpl::adjustOwnerCount : valid object sle type");

    SLE::ref sponsorSle = getLedgerEntryReserveSponsor(ctx.view, objectSle);
    adjustOwnerCountImpl(
        ctx.view,
        ReserveContext::makeFromObj(ctx.view, objectSle, ctx.reserveCtx.accountSle),
        amount,
        j);
}

XRPAmount
accountReserve(
    ReadView const& view,
    SLE::const_ref sle,
    beast::Journal j,
    std::int32_t ownerCountAdj,
    std::int32_t accountCountAdj)
{
    if (!sle)
        Throw<std::runtime_error>("xrpl::accountReserve : valid sle");
    if (sle->getType() != ltACCOUNT_ROOT)
        Throw<std::logic_error>("xrpl::accountReserve : valid sle type");

    std::uint32_t const currentOwnerCount = ownerCount(sle, j, ownerCountAdj);
    std::uint32_t const currentAccountCount = accountCountImpl(sle, accountCountAdj, j);

    return baseAccountReserve(view, currentOwnerCount, currentAccountCount);
}

XRPAmount
baseAccountReserve(ReadView const& view, std::int32_t ownerCount, std::int32_t accountCount)
{
    auto const& fees = view.fees();
    return (fees.reserve * accountCount) + (fees.increment * ownerCount);
}

TER
checkInsufficientReserve(
    ReadView const& view,
    STTx const& tx,
    SLE::const_ref accSle,
    STAmount const& accBalance,
    SLE::const_ref sponsorSle,
    std::int32_t ownerCountAdj,
    std::int32_t accountCountAdj,
    beast::Journal j)
{
    if (sponsorSle)
    {
        auto const sle = view.read(
            keylet::sponsorship(
                sponsorSle->getAccountID(sfAccount), accSle->getAccountID(sfAccount)));

        // A reserve-sponsored tx must carry a sponsor signature
        // (cosigning path) and/or have a pre-existing sponsorship SLE
        // (prefunded path). Absence of both is an internal invariant break.
        if (isReserveSponsored(tx) && !sle && !tx.isFieldPresent(sfSponsorSignature))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (sle)
        {
            auto const ownerCountAllowed = sle->getFieldU32(sfRemainingOwnerCount);
            if (ownerCountAllowed < ownerCountAdj)
                return tecINSUFFICIENT_RESERVE;
        }

        auto const sponsorBalance = sponsorSle->getFieldAmount(sfBalance);
        STAmount const sponsorReserve =
            accountReserve(view, sponsorSle, j, ownerCountAdj, accountCountAdj);

        if (sponsorBalance < sponsorReserve)
            return tecINSUFFICIENT_RESERVE;
    }
    else
    {
        STAmount const reserve = accountReserve(view, accSle, j, ownerCountAdj, accountCountAdj);
        if (accBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }
    return tesSUCCESS;
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
isPseudoAccount(SLE::const_ref sleAcct, std::set<SField const*> const& pseudoFieldFilter)
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
