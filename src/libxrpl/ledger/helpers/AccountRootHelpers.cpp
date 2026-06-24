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
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
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

static FeePayer
getFeePayerHlp(
    ReadView const& view,
    STTx const& tx,
    std::optional<std::reference_wrapper<SLE::const_pointer const>> const& sponsorshipSle)
{
    if (tx.isFieldPresent(sfDelegate))
    {
        AccountID const payerID = tx[sfDelegate];
        return FeePayer{
            .id = payerID,
            .keylet = keylet::account(payerID),
            .balanceField = sfBalance,
            .type = FeePayerType::Delegate};
    }

    if (tx.isFieldPresent(sfSponsor) && isFeeSponsored(tx))
    {
        AccountID const sponsorAccountID = tx.getAccountID(sfSponsor);
        AccountID const sponseeAccountID = tx.getAccountID(sfAccount);
        auto const sponsorshipKeylet = keylet::sponsorship(sponsorAccountID, sponseeAccountID);

        if (sponsorshipSle)
        {
            if (sponsorshipSle->get())
            {
                // pre funded
                if (!sponsorshipKeylet.check(*(sponsorshipSle->get())))
                {
                    Throw<std::logic_error>(
                        "getFeePayerHlp Invalid sponsorship");  // LCOV_EXCL_LINE
                }

                return FeePayer{
                    .id = sponsorAccountID,
                    .keylet = sponsorshipKeylet,
                    .balanceField = sfFeeAmount,
                    .type = FeePayerType::SponsorPreFunded};
            }
        }
        else if (view.exists(sponsorshipKeylet))
        {
            // pre funded
            return FeePayer{
                .id = sponsorAccountID,
                .keylet = sponsorshipKeylet,
                .balanceField = sfFeeAmount,
                .type = FeePayerType::SponsorPreFunded};
        }

        if (!tx.isFieldPresent(sfSponsorSignature))
        {
            Throw<std::logic_error>(
                "Transactor::getFeePayer valid sponsor signature");  // LCOV_EXCL_LINE
        }

        // co-signed
        return FeePayer{
            .id = sponsorAccountID,
            .keylet = keylet::account(sponsorAccountID),
            .balanceField = sfBalance,
            .type = FeePayerType::SponsorCoSigned};
    }

    AccountID const payerID = tx[sfAccount];
    return FeePayer{
        .id = payerID,
        .keylet = keylet::account(payerID),
        .balanceField = sfBalance,
        .type = FeePayerType::Account};
}

FeePayer
getFeePayer(ReadView const& view, STTx const& tx)
{
    return getFeePayerHlp(view, tx, {});
}

// An owner count cannot be negative. If ownerCountAdj would cause a negative
// owner count, clamp the owner count at 0. Similarly for overflow. This
// ownerCountAdj allows the ownerCount to be adjusted up or down in multiple steps.
// If id != std::nullopt, then do error reporting.
//
// Returns adjusted owner count.
static std::uint32_t
confineOwnerCount(
    std::uint32_t current,
    std::int32_t ownerCountAdj,
    std::optional<AccountID> const& id = std::nullopt,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
{
    std::uint32_t adjusted{current + ownerCountAdj};
    if (ownerCountAdj > 0)
    {
        // Overflow is well defined on unsigned
        if (adjusted < current)
        {
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count exceeds max!";
            }
            adjusted = std::numeric_limits<std::uint32_t>::max();
        }
    }
    else
    {
        // Underflow is well defined on unsigned
        if (adjusted > current)
        {
            if (id)
            {
                JLOG(j.fatal()) << "Account " << *id << " owner count set below 0!";
            }
            adjusted = 0;
            XRPL_ASSERT(!id, "xrpl::confineOwnerCount : id is not set");
        }
    }
    return adjusted;
}

std::expected<std::uint32_t, bool>
baseOwnerCount(
    std::uint32_t ownerCount,
    std::uint32_t sponsoredCount,
    std::uint32_t sponsoringCount)
{
    int64_t const x = static_cast<int64_t>(ownerCount) - sponsoredCount + sponsoringCount;
    if (x < 0)
        return std::unexpected(false);
    if (x > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(true);
    return static_cast<std::uint32_t>(x);
}

static std::uint32_t
ownerCountHlp(
    ReadView const& view,
    SLE::const_ref sle,
    std::int32_t ownerCountAdj,
    bool reportConfine,
    beast::Journal j)
{
    AccountID const id = sle->at(sfAccount);
    OwnerCounts const currentCount(*sle);
    auto const hookedCount = view.ownerCountHook(id, currentCount);

    if (!hookedCount.valid())
        Throw<std::logic_error>("xrpl::ownerCountHlp : Invalid OwnerCount ");  // LCOV_EXCL_LINE

    std::int64_t deltaCount =
        static_cast<std::int64_t>(ownerCountAdj) - hookedCount.sponsored + hookedCount.sponsoring;
    if (deltaCount > std::numeric_limits<std::int32_t>::max())
    {
        deltaCount = std::numeric_limits<std::int32_t>::max();
        JLOG(j.error()) << "Account " << id << " adjustment exceeds max, "
                        << "Owner count: " << hookedCount.owner << ", adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << hookedCount.sponsored
                        << ", sponsoringCount: " << hookedCount.sponsoring;
    }
    else if (deltaCount < std::numeric_limits<std::int32_t>::min())
    {
        deltaCount = std::numeric_limits<std::int32_t>::min();
        JLOG(j.error()) << "Account " << id << " adjustment exceeds min, "
                        << "Owner count: " << hookedCount.owner << ", adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << hookedCount.sponsored
                        << ", sponsoringCount: " << hookedCount.sponsoring;
    }

    std::uint32_t const confinedCount = reportConfine
        ? confineOwnerCount(hookedCount.owner, deltaCount, id, j)
        : confineOwnerCount(hookedCount.owner, deltaCount);

    return confinedCount;
}

static std::uint32_t
reserveCountHlp(SLE::const_ref sle, std::int32_t reserveCountAdj, beast::Journal j)
{
    AccountID const id = sle->at(sfAccount);
    bool const isSponsored = sle->isFieldPresent(sfSponsor);
    std::int64_t const sponsoringCount = sle->at(sfSponsoringAccountCount);
    std::int64_t const reserveCount = (isSponsored ? 0 : 1) + sponsoringCount;

    // Ensure adjusted value fit limits. Behave like confineOwnerCount
    std::int64_t adjusted = reserveCount + reserveCountAdj;
    if (adjusted > std::numeric_limits<std::uint32_t>::max())
    {
        JLOG(j.error()) << "Account " << id << " reserve count exceeds max, "
                        << "adjustment: " << reserveCountAdj
                        << ", sponsoringCount: " << sponsoringCount
                        << ", reserveCount: " << reserveCount;
        adjusted = std::numeric_limits<std::uint32_t>::max();
    }
    else if (adjusted < 0)
    {
        JLOG(j.fatal()) << "Account " << id << " reserve count below 0, "
                        << "adjustment: " << reserveCountAdj
                        << ", sponsoringCount: " << sponsoringCount
                        << ", reserveCount: " << reserveCount;
        adjusted = 0;
    }

    return static_cast<std::uint32_t>(adjusted);
}

static inline XRPAmount
baseReserveHlp(ReadView const& view, std::uint32_t ownerCount, std::uint32_t reserveCount)
{
    auto const& fees = view.fees();
    return (fees.reserve * reserveCount) + (fees.increment * ownerCount);
}

// returns {reserve, ownerCount, reserveCount}
static std::tuple<XRPAmount, std::uint32_t, std::uint32_t>
accountReserveHlp(
    ReadView const& view,
    SLE::const_ref accSle,
    std::uint32_t ownerCountAdj,
    std::uint32_t reserveCountAdj,
    bool reportConfine,
    beast::Journal j)
{
    // Pseudo-accounts have no reserve requirement
    if (isPseudoAccount(accSle))
        return {XRPAmount(), 0, 0};

    std::uint32_t const ownerCount = ownerCountHlp(view, accSle, ownerCountAdj, reportConfine, j);
    std::uint32_t const reserveCount = reserveCountHlp(accSle, reserveCountAdj, j);
    auto const reserve = baseReserveHlp(view, ownerCount, reserveCount);
    return {reserve, ownerCount, reserveCount};
}

std::uint32_t
ownerCount(
    ReadView const& view,
    SLE::const_ref accSle,
    beast::Journal j,
    std::int32_t ownerCountAdj)
{
    if (!accSle)
        Throw<std::logic_error>("xrpl::ownerCount: empty sle type");  // LCOV_EXCL_LINE

    auto const sleType = accSle->getType();
    bool const validType = sleType == ltLOAN_BROKER || sleType == ltACCOUNT_ROOT;
    if (!validType)
        Throw<std::logic_error>("xrpl::ownerCount: valid sle type");  // LCOV_EXCL_LINE

    return ownerCountHlp(view, accSle, ownerCountAdj, true, j);
}

static XRPAmount
xrpLiquidHlp(
    ReadView const& view,
    SLE::const_ref accSle,
    std::int32_t ownerCountAdj,
    std::int32_t reserveCountAdj,
    std::pair<AccountID, XRPAmount> feeAdj,
    beast::Journal j)
{
    AccountID const id = accSle->at(sfAccount);
    auto [reserve, ownerCount, reserveCount] =
        accountReserveHlp(view, accSle, ownerCountAdj, reserveCountAdj, false, j);

    STAmount const fullBalance = accSle->at(sfBalance);
    XRPAmount const balance = view.balanceHookIOU(id, xrpAccount(), fullBalance).xrp();

    XRPAmount const fee = feeAdj.first == id ? feeAdj.second : XRPAmount();
    XRPAmount const preFeeBalance = balance + fee;
    XRPAmount const amount = preFeeBalance - std::max(reserve, fee);

    JLOG(j.trace()) << "accountHolds:" << " account=" << to_string(id)
                    << " amount=" << amount.decimalXRP()
                    << " fullBalance=" << fullBalance.getFullText()
                    << " balance=" << balance.decimalXRP() << " reserve=" << reserve.decimalXRP()
                    << " ownerCount=" << ownerCount << " ownerCountAdj=" << ownerCountAdj
                    << " reserveCount=" << reserveCount << " reserveCountAdj=" << reserveCountAdj
                    << " fee adj=" << fee.decimalXRP();
    return amount;
}

XRPAmount
xrpLiquid(ReadView const& view, SLE::const_ref accSle, std::int32_t ownerCountAdj, beast::Journal j)
{
    if (!accSle)
        return beast::kZero;

    auto const x =
        xrpLiquidHlp(view, accSle, ownerCountAdj, 0, std::pair<AccountID, XRPAmount>(), j);
    return x.negative() ? XRPAmount() : x;
}

Rate
transferRate(ReadView const& view, AccountID const& issuer)
{
    auto const sle = view.read(keylet::account(issuer));

    if (sle && sle->isFieldPresent(sfTransferRate))
        return Rate{sle->getFieldU32(sfTransferRate)};

    return kParityRate;
}

static std::uint32_t
adjustOwnerCountHlp(
    ApplyView& view,
    SLE::ref sle,
    SF_UINT32 const& sfield,
    AccountID const& accID,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    std::uint32_t const current = sle->at(sfield);
    std::uint32_t const adjusted = confineOwnerCount(current, ownerCountAdj, accID, j);
    sle->at(sfield) = adjusted;
    view.update(sle);
    return adjusted;
}

void
adjustOwnerCount(
    ApplyView& view,
    SLE::ref accountSle,
    SLE::ref sponsorSle,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    if (!accountSle)
        Throw<std::runtime_error>("xrpl::adjustOwnerCount : valid account sle");  // LCOV_EXCL_LINE

    auto const sleType = accountSle->getType();
    bool const validType = sponsorSle ? (sleType == ltACCOUNT_ROOT)
                                      : (sleType == ltLOAN_BROKER || sleType == ltACCOUNT_ROOT);
    if (!validType)
    {
        Throw<std::logic_error>(
            "xrpl::adjustOwnerCount : valid account sle type");  // LCOV_EXCL_LINE
    }

    XRPL_ASSERT(ownerCountAdj, "xrpl::adjustOwnerCount : nonzero adjustment input");
    if (ownerCountAdj == 0)
        return;

    OwnerCounts const current(sleType == ltACCOUNT_ROOT ? OwnerCounts(*accountSle) : OwnerCounts());
    OwnerCounts adjusted(current);

    auto const accountID = accountSle->getAccountID(sfAccount);
    if (sponsorSle)
    {
        if (sponsorSle->getType() != ltACCOUNT_ROOT)
        {
            Throw<std::logic_error>(
                "xrpl::adjustOwnerCount : valid sponsor sle type");  // LCOV_EXCL_LINE
        }
        auto const sponsorID = sponsorSle->getAccountID(sfAccount);

        if (accountID == sponsorID)
        {
            Throw<std::logic_error>(
                "adjustOwnerCount : account can't be sponsor for themself");  // LCOV_EXCL_LINE
        }

        adjusted.sponsored = adjustOwnerCountHlp(
            view, accountSle, sfSponsoredOwnerCount, accountID, ownerCountAdj, j);

        {
            OwnerCounts const sponsorCurrent(*sponsorSle);
            OwnerCounts sponsorAdjustment(sponsorCurrent);
            sponsorAdjustment.sponsoring = adjustOwnerCountHlp(
                view, sponsorSle, sfSponsoringOwnerCount, sponsorID, ownerCountAdj, j);
            view.adjustOwnerCountHook(sponsorID, sponsorCurrent, sponsorAdjustment);
        }

        auto sponsorshipSle = view.peek(keylet::sponsorship(sponsorID, accountID));
        if (sponsorshipSle && ownerCountAdj > 0)
        {
            // Only decrease the pre-funded ReserveCount on Sponsorship if we assign new objects.
            // Removing/reassigning ownership of the object doesn't increase RemainingOwnerCount
            // back. Don't call hook because this counter is not something that require reserve
            // (like other sf...OwnerCounts do).
            adjustOwnerCountHlp(
                view, sponsorshipSle, sfRemainingOwnerCount, sponsorID, -ownerCountAdj, j);
        }
    }

    adjusted.owner =
        adjustOwnerCountHlp(view, accountSle, sfOwnerCount, accountID, ownerCountAdj, j);
    if (sleType == ltACCOUNT_ROOT)
        view.adjustOwnerCountHook(accountID, current, adjusted);
}

void
adjustOwnerCountDeleteObj(
    ApplyView& view,
    SLE::ref accountSle,
    SLE::ref objectSle,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    if (!objectSle)
    {
        Throw<std::runtime_error>(
            "xrpl::adjustOwnerCountDeleteObj : valid object sle");  // LCOV_EXCL_LINE
    }
    if (objectSle->getType() == ltACCOUNT_ROOT)
    {
        Throw<std::logic_error>(
            "xrpl::adjustOwnerCountDeleteObj : valid object sle type");  // LCOV_EXCL_LINE
    }
    if (ownerCountAdj >= 0)
    {
        Throw<std::logic_error>(
            "xrpl::adjustOwnerCountDeleteObj : adjustment >= 0");  // LCOV_EXCL_LINE
    }

    XRPL_ASSERT(ownerCountAdj, "xrpl::adjustOwnerCount : nonzero adjustment input");
    if (ownerCountAdj == 0)
        return;

    SLE::ref sponsorSle = getLedgerEntryReserveSponsor(view, objectSle);
    adjustOwnerCount(view, accountSle, sponsorSle, ownerCountAdj, j);
}

XRPAmount
accountReserve(
    ReadView const& view,
    SLE::const_ref sle,
    beast::Journal j,
    std::int32_t ownerCountAdj,
    std::int32_t reserveCountAdj)
{
    if (!sle)
        Throw<std::runtime_error>("xrpl::accountReserve : valid sle");  // LCOV_EXCL_LINE
    if (sle->getType() != ltACCOUNT_ROOT)
        Throw<std::logic_error>("xrpl::accountReserve : valid sle type");  // LCOV_EXCL_LINE

    [[maybe_unused]] auto [reserve, _ownerCount, _reserveCount] =
        accountReserveHlp(view, sle, ownerCountAdj, reserveCountAdj, true, j);

    return reserve;
}

XRPAmount
baseAccountReserve(ReadView const& view, std::int32_t ownerCount)
{
    auto const reserve = baseReserveHlp(view, ownerCount, 1);
    return reserve;
}

static TER
checkXrpBalanceGeneral(
    ReadView const& view,
    bool apply,
    STTx const& tx,
    SLE::const_ref accSle,
    std::optional<XRPAmount> const& balanceAcc,
    SLE::const_ref sponsorSle,
    std::int32_t ownerCountAdj,
    std::int32_t reserveCountAdj,
    XRPAmount balanceAdj,
    bool skipIfOwnerCountBelow2,
    beast::Journal j,
    bool checkApplicability)
{
    // Passed 'balance' means checks are on caller, needs for some non-standard checks
    if (balanceAcc && balanceAdj)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (balanceAcc && balanceAcc->negative())
        return tecINSUFFICIENT_FUNDS;

    XRPL_ASSERT(
        !skipIfOwnerCountBelow2 || (!balanceAcc && !balanceAdj), "small owner count with balance");

    // With sponsored account reserve requirements can be 0. But some checks for liquidity assume we
    // have reserve and we can borrow fee from it in edge cases. We can end up in potential negative
    // balance. Here is 'apply' for - to distinguish if sfBalance contains preFee or postFee amount.
    // Then fee participating in xrpLiquid calculation.
    XRPAmount const feePayed(apply ? tx[sfFee].xrp() : XRPAmount());
    AccountID feePayer;

    bool const isDelegating = tx.isFieldPresent(sfDelegate);
    bool const isCoSigning = isSponsorReserveCoSigning(tx);
    bool sponsored = false;

    // !delegated || isCoSigning - delegate doesn't allow sponsorship, check accSle for reserve
    if (sponsorSle && (!isDelegating || isCoSigning))
    {
        auto const accID = accSle->at(sfAccount);
        // Check if sponsor applicable (for manually passed sponsorSle)
        if (checkApplicability && (accID != tx[sfAccount]))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        AccountID const sponsorID = sponsorSle->at(sfAccount);

        bool const skipSponsorshipReserve = isDelegating || (ownerCountAdj <= 0);
        auto const sponsorshipSle = !skipSponsorshipReserve
            ? view.read(keylet::sponsorship(sponsorID, accID))
            : SLE::pointer();

        // Sponsorship have priority before co-signing
        if (!skipSponsorshipReserve)
        {
            if (!isCoSigning && !sponsorshipSle)  // checked in Transactor::checkSponsor
                return tecINTERNAL;               // LCOV_EXCL_LINE

            if (sponsorshipSle)
            {
                std::uint32_t const remainingOwnerCount = sponsorshipSle->at(sfRemainingOwnerCount);
                if (std::cmp_less(remainingOwnerCount, ownerCountAdj))
                    return tecINSUFFICIENT_RESERVE;
            }
        }

        feePayer = getFeePayerHlp(view, tx, sponsorshipSle).id;

        // co-signing or pre-fund still check sponsor capabilities
        auto const sponsorLiquid =
            xrpLiquidHlp(view, sponsorSle, ownerCountAdj, reserveCountAdj, {feePayer, feePayed}, j);
        if (sponsorLiquid.negative())
            return tecINSUFFICIENT_RESERVE;

        sponsored = true;
    }
    else
    {
        // Special case for amm/trustlines/authorizeMPtoken -  to not to demand reserve if
        // ownerCount less than 2. Sponsor still check reserve for full count.
        if (skipIfOwnerCountBelow2 && (ownerCountHlp(view, accSle, 0, true, j) < 2))
            return tesSUCCESS;

        feePayer = getFeePayerHlp(view, tx, {}).id;
    }

    auto const oca = sponsored ? 0 : ownerCountAdj;
    auto const rca = sponsored ? 0 : reserveCountAdj;

    if (balanceAcc)
    {
        // balance passed, fee checks on caller, just check for reserve
        [[maybe_unused]] auto [reserve, _1, _2] =
            accountReserveHlp(view, accSle, oca, rca, true, j);
        XRPAmount const accLiquid = *balanceAcc - reserve;
        if (accLiquid.negative())
            return tecINSUFFICIENT_RESERVE;
        return tesSUCCESS;
    }

    {
        XRPAmount const accLiquid = xrpLiquidHlp(view, accSle, oca, rca, {feePayer, feePayed}, j);
        auto const accAdjusted = accLiquid + balanceAdj;
        // positive balance can improve liquidity
        if (accLiquid.negative() && accAdjusted.negative())
            return tecINSUFFICIENT_RESERVE;
        if (accAdjusted.negative())
            return tecINSUFFICIENT_FUNDS;
    }

    return tesSUCCESS;
}

TER
checkXrpBalanceHlp(
    ReadView const& view,
    bool apply,
    STTx const& tx,
    std::optional<AccountID> const& accID,
    std::optional<std::reference_wrapper<SLE::const_pointer const>> const& accOpt,
    std::optional<XRPAmount> const& balanceAcc,
    std::optional<std::reference_wrapper<SLE::const_pointer const>> const& sponsorOpt,
    std::int32_t ownerCountAdj,
    std::int32_t reserveCountAdj,
    XRPAmount balanceAdj,
    bool skipIfOwnerCountBelow2,
    beast::Journal j,
    bool checkApplicability)
{
    if ((!accID && !accOpt) || (accID && accOpt))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    SLE::const_ref accSle = !accOpt ? view.read(keylet::account(*accID)) : accOpt->get();
    if (!accSle || (accSle->getType() != ltACCOUNT_ROOT))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    SLE::const_ref sponsorSle = !sponsorOpt
        ? getTxReserveSponsor(
              view,
              tx,
              checkApplicability ? std::make_optional(accSle->at(sfAccount)) : std::nullopt)
        : sponsorOpt->get();

    return checkXrpBalanceGeneral(
        view,
        apply,
        tx,
        accSle,
        balanceAcc,
        sponsorSle,
        ownerCountAdj,
        reserveCountAdj,
        balanceAdj,
        skipIfOwnerCountBelow2,
        j,
        checkApplicability);
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
