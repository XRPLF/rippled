#include <xrpl/ledger/entries/AccountRootEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OwnerCounts.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace xrpl {

template <typename ViewT>
XRPAmount
AccountRootEntry<ViewT>::reserve(Adjustment adj) const
{
    auto const& view = this->readView();
    auto const j = this->journal();

    XRPL_ASSERT(
        this->exists() && (*this)->getType() == ltACCOUNT_ROOT, "xrpl::accountReserve : valid sle");

    if (!view.rules().enabled(featureSponsor))
    {
        XRPL_ASSERT(adj.accountCountDelta == 0, "xrpl::accountReserve : no account count delta");
        return view.fees().accountReserve(
            (*this)->getFieldU32(sfOwnerCount) + adj.ownerCountDelta, 1);
    }
    std::uint32_t const currentOwnerCount = ownerCount(adj.ownerCountDelta);
    std::uint32_t const currentAccountCount =
        detail::accountCountImpl(*this, adj.accountCountDelta, j);

    return view.fees().accountReserve(currentOwnerCount, currentAccountCount);
}

template <typename ViewT>
std::uint32_t
AccountRootEntry<ViewT>::ownerCount(std::int32_t ownerCountAdj) const
{
    auto const j = this->journal();

    XRPL_ASSERT(
        this->exists() && (*this)->getType() == ltACCOUNT_ROOT,
        "xrpl::ownerCount : sle is account root");

    AccountID const id = (*this)->getAccountID(sfAccount);
    std::uint32_t const currentOwnerCount = (*this)->at(sfOwnerCount);
    std::uint32_t const sponsoredOwnerCount = (*this)->at(sfSponsoredOwnerCount);
    std::uint32_t const sponsoringOwnerCount = (*this)->at(sfSponsoringOwnerCount);

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

    return detail::confineOwnerCount(currentOwnerCount, deltaCount);
}

template <typename ViewT>
TER
AccountRootEntry<ViewT>::checkReserve(
    ApplyViewContext ctx,
    XRPAmount accBalance,
    std::optional<AccountRootEntry<ReadView>> const& sponsorSle,
    Adjustment adj,
    TER insufReserveCode) const
{
    // TODO: swap to assert after fixCleanup3_2_0 is retired
    if (!this->exists() || (*this)->getType() != ltACCOUNT_ROOT)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    XRPL_ASSERT(
        !isTesSuccess(insufReserveCode), "xrpl::checkReserve : insufReserveCode is not tesSUCCESS");
    if (ctx.view.rules().enabled(featureSponsor))
    {
        if (sponsorSle)
        {
            if ((*sponsorSle)->getType() != ltACCOUNT_ROOT)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            auto const sle = ctx.view.read(
                keylet::sponsorship(
                    (*sponsorSle)->getAccountID(sfAccount), (*this)->getAccountID(sfAccount)));

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

            auto const sponsorBalance = (*sponsorSle)->getFieldAmount(sfBalance).xrp();
            XRPAmount const sponsorReserve = sponsorSle->reserve(adj);

            if (sponsorBalance < sponsorReserve)
                return insufReserveCode;
        }
        else
        {
            XRPAmount const reserve = this->reserve(adj);
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
            (*this)->getFieldU32(sfOwnerCount) + adj.ownerCountDelta, 1);
        if (accBalance < reserve)
            return insufReserveCode;
    }
    return tesSUCCESS;
}

template <typename ViewT>
TER
AccountRootEntry<ViewT>::checkReserve(ApplyViewContext ctx, XRPAmount accBalance, Adjustment adj)
    const
{
    auto const sponsorExp = getEffectiveTxReserveSponsor(ctx, *this);
    if (!sponsorExp)
        return sponsorExp.error();  // LCOV_EXCL_LINE

    std::optional<AccountRootEntry<ReadView>> sponsorSle;
    if (*sponsorExp)
        sponsorSle.emplace(**sponsorExp);
    return checkReserve(ctx, accBalance, sponsorSle, adj);
}

template <typename ViewT>
bool
AccountRootEntry<ViewT>::isPseudoAccount() const
{
    // Intentionally use defensive coding here because it's cheap and makes the
    // semantics of true return value clean.
    return this->exists() && (*this)->getType() == ltACCOUNT_ROOT &&
        std::ranges::any_of(getPseudoAccountFields(), [this](SField const* sf) {
               return (*this)->isFieldPresent(*sf);
           });
}

template <typename ViewT>
void
AccountRootEntry<ViewT>::adjustOwnerCountSigned(
    std::optional<AccountRootEntry<ApplyView>>& sponsorSle,
    std::int32_t adjustment)
    requires Base::kIsWritable
{
    auto& view = this->applyView();
    auto const j = this->journal();

    if (view.rules().enabled(featureSponsor))
    {
        XRPL_ASSERT(this->exists(), "xrpl::adjustOwnerCountSigned : valid account sle");
        if (!this->exists())
            return;  // LCOV_EXCL_LINE

        auto const accountID = (*this)->getAccountID(sfAccount);
        bool const validType = (*this)->getType() == ltACCOUNT_ROOT;
        XRPL_ASSERT(validType, "xrpl::adjustOwnerCountSigned : valid account sle type");
        if (!validType)
            return;  // LCOV_EXCL_LINE

        XRPL_ASSERT(adjustment, "xrpl::adjustOwnerCountSigned : nonzero adjustment input");

        OwnerCounts const currentOwnerCount(this->rawSle());
        OwnerCounts totalOwnerCount(currentOwnerCount);

        if (sponsorSle)
        {
            bool const validSponsorType = (*sponsorSle)->getType() == ltACCOUNT_ROOT;
            XRPL_ASSERT(validSponsorType, "xrpl::adjustOwnerCountSigned : valid sponsor sle type");
            if (!validSponsorType)
                return;  // LCOV_EXCL_LINE
            auto const sponsorID = (*sponsorSle)->getAccountID(sfAccount);

            totalOwnerCount.sponsored = detail::adjustOwnerCountImpl(
                view, this->mutableRawSle(), sfSponsoredOwnerCount, accountID, adjustment, j);

            {
                OwnerCounts const sponsorCurrent(sponsorSle->rawSle());
                OwnerCounts sponsorAdjustment(sponsorCurrent);
                sponsorAdjustment.sponsoring = detail::adjustOwnerCountImpl(
                    view,
                    sponsorSle->mutableRawSle(),
                    sfSponsoringOwnerCount,
                    sponsorID,
                    adjustment,
                    j);
                view.adjustOwnerCountHook(sponsorID, sponsorCurrent, sponsorAdjustment);
            }

            auto sponsorshipSle = view.peek(keylet::sponsorship(sponsorID, accountID));
            if (sponsorshipSle && adjustment > 0)
            {
                // Only decrease the pre-funded ReserveCount on Sponsorship if we assign new
                // objects. Removing/reassigning ownership of the object doesn't increase
                // RemainingOwnerCount back. Don't call hook because this counter is not something
                // that requires reserve (like other sf...OwnerCounts do).
                detail::adjustOwnerCountImpl(
                    view, sponsorshipSle, sfRemainingOwnerCount, sponsorID, -adjustment, j);
            }
        }

        totalOwnerCount.owner = detail::adjustOwnerCountImpl(
            view, this->mutableRawSle(), sfOwnerCount, accountID, adjustment, j);
        view.adjustOwnerCountHook(accountID, currentOwnerCount, totalOwnerCount);
    }
    else
    {
        XRPL_ASSERT(this->exists(), "xrpl::adjustOwnerCountSigned : valid account sle");
        if (!this->exists())
            return;
        // the remaining are only asserts to preserve existing behavior
        XRPL_ASSERT(!sponsorSle, "xrpl::adjustOwnerCountSigned : sponsor not enabled");
        XRPL_ASSERT(
            (*this)->getType() == ltACCOUNT_ROOT,
            "xrpl::adjustOwnerCountSigned : valid account sle type");
        XRPL_ASSERT(adjustment, "xrpl::adjustOwnerCount : nonzero adjustment input");
        std::uint32_t const current{(*this)->getFieldU32(sfOwnerCount)};
        AccountID const id = (**this)[sfAccount];
        std::uint32_t const adjusted = detail::confineOwnerCount(current, adjustment, id, j);

        OwnerCounts const currentOwnerCount(this->rawSle());
        OwnerCounts finalOwnerCount(currentOwnerCount);
        finalOwnerCount.owner = adjusted;

        view.adjustOwnerCountHook(id, currentOwnerCount, finalOwnerCount);
        (*this)->at(sfOwnerCount) = adjusted;
        this->update();
    }
}

template <typename ViewT>
void
AccountRootEntry<ViewT>::increaseOwnerCount(
    std::optional<AccountRootEntry<ApplyView>>& sponsorSle,
    std::uint32_t count)
    requires Base::kIsWritable
{
    XRPL_ASSERT(
        count != 0 && count <= std::numeric_limits<std::int32_t>::max(),
        "xrpl::increaseOwnerCount : count in signed delta range");
    if (count == 0 || count > std::numeric_limits<std::int32_t>::max())
        return;  // LCOV_EXCL_LINE

    adjustOwnerCountSigned(sponsorSle, static_cast<std::int32_t>(count));
}

template <typename ViewT>
void
AccountRootEntry<ViewT>::increaseOwnerCount(ApplyViewContext ctx, std::uint32_t count)
    requires Base::kIsWritable
{
    auto sponsorExp = getEffectiveTxReserveSponsor(ctx, *this);

    // The sponsor's existence is validated by checkReserve/checkSponsor before
    // any owner-count mutation, so loading it here cannot fail.
    XRPL_ASSERT(
        sponsorExp.has_value(), "xrpl::increaseOwnerCount : sponsor validated before mutation");

    std::optional<AccountRootEntry<ApplyView>> sponsorSle;
    if (sponsorExp && *sponsorExp)
        sponsorSle.emplace(std::move(**sponsorExp));

    increaseOwnerCount(sponsorSle, count);
}

template <typename ViewT>
void
AccountRootEntry<ViewT>::decreaseOwnerCount(
    std::optional<AccountRootEntry<ApplyView>>& sponsorSle,
    std::uint32_t count)
    requires Base::kIsWritable
{
    XRPL_ASSERT(
        count != 0 && count <= std::numeric_limits<std::int32_t>::max(),
        "xrpl::decreaseOwnerCount : count in signed delta range");
    if (count == 0 || count > std::numeric_limits<std::int32_t>::max())
        return;  // LCOV_EXCL_LINE

    adjustOwnerCountSigned(sponsorSle, -static_cast<std::int32_t>(count));
}

template class AccountRootEntry<ReadView>;
template class AccountRootEntry<ApplyView>;

}  // namespace xrpl
