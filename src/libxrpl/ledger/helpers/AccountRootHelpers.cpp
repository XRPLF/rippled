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
#include <xrpl/ledger/entries/AccountRootEntry.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace xrpl {

bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer)
{
    if (isXRP(issuer))
        return false;
    if (auto const sle = AccountRootEntryR(issuer, view))
        return sle->isFlag(lsfGlobalFreeze);
    return false;
}

namespace detail {

std::uint32_t
confineOwnerCount(
    std::uint32_t currentOwnerCount,
    std::int32_t ownerCountAdj,
    std::optional<AccountID> const& id,
    beast::Journal j)
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

std::uint32_t
accountCountImpl(AccountRootEntryR const& sle, std::int32_t accountCountAdj, beast::Journal j)
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
    SLE::Ref sle,
    SF_UINT32 const& sfield,
    AccountID const& accID,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    std::uint32_t const currentOwnerCount = sle->at(sfield);
    std::uint32_t const totalOwnerCount =
        detail::confineOwnerCount(currentOwnerCount, ownerCountAdj, accID, j);
    sle->at(sfield) = totalOwnerCount;
    view.update(sle);
    return totalOwnerCount;
}

}  // namespace detail

XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j)
{
    auto const sle = AccountRootEntryR(id, view);
    if (!sle.exists())
        return beast::kZero;

    // Return balance minus reserve
    std::uint32_t const currentOwnerCount = detail::confineOwnerCount(
        view.ownerCountHook(id, OwnerCounts(sle.rawSle())).count(), ownerCountAdj);
    std::uint32_t const currentAccountCount = detail::accountCountImpl(sle, 0, j);

    // Pseudo-accounts have no reserve requirement
    auto const reserve = sle.isPseudoAccount()
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
    auto const sle = AccountRootEntryR(issuer, view);

    if (sle && sle->isFieldPresent(sfTransferRate))
        return Rate{sle->getFieldU32(sfTransferRate)};

    return kParityRate;
}

void
decreaseOwnerCountForObject(
    ApplyView& view,
    AccountRootEntryW& accountSle,
    SLE::Ref objectSle,
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

    auto sponsorSle = getLedgerEntryReserveSponsor(view, objectSle);
    accountSle.decreaseOwnerCount(sponsorSle, count);
}

void
adjustLoanBrokerOwnerCount(
    ApplyView& view,
    SLE::Ref brokerSle,
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

    detail::adjustOwnerCountImpl(
        view, brokerSle, sfOwnerCount, brokerSle->getAccountID(sfAccount), delta, j);
}

// ----------------------------------------------------

AccountID
pseudoAccountAddress(ReadView const& view, UInt256 const& pseudoOwnerKey)
{
    // This number must not be changed without an amendment
    static constexpr std::uint16_t kMaxAccountAttempts = 256;
    for (std::uint16_t i = 0; i < kMaxAccountAttempts; ++i)
    {
        RipeshaHasher rsh;
        auto const hash = sha512Half(i, view.header().parentHash, pseudoOwnerKey);
        rsh(hash.data(), hash.size());
        AccountID const ret = AccountID::fromRaw(static_cast<RipeshaHasher::result_type>(rsh));
        if (!AccountRootEntryR(ret, view).exists())
            return ret;
    }
    return beast::kZero;
}

// Pseudo-account designator fields MUST be maintained by including the
// SField::kSmdPseudoAccount flag in the SField definition. (Don't forget to
// "| SField::kSmdDefault"!) The fields do NOT need to be amendment-gated,
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

[[nodiscard]] TER
checkDestinationAndTag(AccountRootEntryR const& toSle, bool hasDestinationTag)
{
    if (!toSle.exists())
        return tecNO_DST;

    // The tag is basically account-specific information we don't
    // understand, but we can require someone to fill it in.
    if (toSle->isFlag(lsfRequireDestTag) && !hasDestinationTag)
        return tecDST_TAG_NEEDED;  // Cannot send without a tag

    return tesSUCCESS;
}

}  // namespace xrpl
